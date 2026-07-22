#include <fcntl.h>



#include <syslog.h>



#include <sys/types.h>

#include <sys/socket.h>



#include <errno.h>





#include <netdb.h>



#include <stdlib.h>



#include <arpa/inet.h>



#include <unistd.h>



#include <pthread.h>



#include <signal.h>



#include <string.h>

#include <stdio.h>

#include <stdbool.h>



#include <stdatomic.h>



#include <sys/stat.h>

#include <sys/queue.h>

#include "aesd_ioctl.h"

#define SIZE_OF_RAM_BUFFER	1000



//------------------------------------------

// Static variables

//------------------------------------------

static int socket_fd = 0;

static pthread_mutex_t output_fd_mutex;



static volatile sig_atomic_t signal_exit_requested;

static atomic_bool periodic_task_error_exit_requested;



//------------------------------------------

// Typedefs

//------------------------------------------

typedef struct node{

    pthread_t thread_id;

    int client_fd;

    atomic_bool client_done; //  0 = closed, 1 = open

    SLIST_ENTRY(node) entries;

    char client_ip[INET_ADDRSTRLEN];

}node_t;



SLIST_HEAD(node_head, node);



struct node_head head;



#if(USE_AESD_CHAR_DEVICE != 1)

static const char output_filename[] = "/var/tmp/aesdsocketdata";

#else

static const char output_filename[] = "/dev/aesdchar";

#endif





//------------------------------------------

// Static function prototypes

//------------------------------------------

static void daemonize(void);



static void *process_client(void *client_node);



static void clean_up_closed_client_threads(void);

static void clean_up_all_client_threads(void);

static void clean_up_client_thread(node_t *client_node);



static void start_client_thread(int client_fd, char *ip);



static int write_to_file(int output_fd,

                        char *buf,

                        size_t num_bytes_to_write);



static int  send_file_data_to_client(int output_fd, int client_fd);



static void signal_handler(int signo);

static int setup_signal_handlers(void);



static void print_syscall_error(char *error_msg, int error_number);



#if(USE_AESD_CHAR_DEVICE != 1)

static void timestamp_thread_wake_handler(int signo);

static void *periodic_thread(void *arg);

#endif





#if (USE_AESD_CHAR_DEVICE == 1) // Debug message

#warning "AESD SOCKET BUILDING WITH CHAR DEVICE MODE"

#else

#warning "AESD SOCKET BUILDING WITH FILE/TIMESTAMP MODE"

#endif





int main(int argc, char *argv[]){

    

    #if(USE_AESD_CHAR_DEVICE != 1)

    // Remove output file if already exist

    if ((remove(output_filename) == -1) && (errno != ENOENT)) { // MUST: What happens if 2 isntances of this program run

		print_syscall_error("remove of file failed: %s", errno);

        goto ERROR;

	}

    #endif

    

	// Enable logging

	openlog("aesdsocket", LOG_PID, LOG_DAEMON);

	

	// Register signal handlers

	if(setup_signal_handlers() == -1){

        goto ERROR;

    }

	

	// create a socket

	socket_fd = socket(AF_INET, SOCK_STREAM, 0);

	if(socket_fd == -1){

		print_syscall_error("Could not create socket: %s", errno);

        goto FAIL_SOCKET;

	}



	int opt = 1;	

	setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)); // MUST: Check return?



	// bind socket

	int status;

	struct addrinfo hints;

	struct addrinfo *servinfo; // will point to the results

	

	memset(&hints, 0, sizeof(hints)); // make sure struct is empty

	hints.ai_family = AF_UNSPEC; // Don't care IPv4 or IPv6

	hints.ai_socktype = SOCK_STREAM;	// TCP stream sockets

	hints.ai_flags = AI_PASSIVE;	// fill in my IP for me



	if((status = getaddrinfo(NULL, "9000", &hints, &servinfo)) != 0){

		print_syscall_error("Could not getaddrinfo: %s", errno);

        goto FAIL_SOCKET;

	}

	

	// servinfo now points to a linked list of 1 or more struct addrinfos



	if((status = bind(socket_fd, servinfo->ai_addr, servinfo->ai_addrlen)) != 0){

		print_syscall_error("Could not bind: %s", errno);

        goto FAIL_SOCKET;

	}

	

	freeaddrinfo(servinfo);	// no use of servinfo now, free



	if(argc == 2){

		if(strcmp(argv[1], "-d") == 0){

			daemonize();    // MUST: Handle errors

		}

	}

	

	// Listen for new connections

	if((status = listen(socket_fd, 5)) != 0){

		print_syscall_error("Could not listen: %s", errno);

        goto FAIL_SOCKET;

	}





    #if(USE_AESD_CHAR_DEVICE != 1)

    // Create period task to log timestamp

    pthread_t timestamp_thread; 

    if(pthread_create(&(timestamp_thread), NULL, periodic_thread, NULL) != 0){

        print_syscall_error("Could not timestamp_log thread: %s", errno);

        goto FAIL_SOCKET;

    }   

    #endif 



    // Initialize linked list to store threads

    SLIST_INIT(&head);  // MUST: Handle return values/errors of Linked List macros



    pthread_mutex_init(&output_fd_mutex, NULL); // MUST: Check return???



	while((!signal_exit_requested) && (!atomic_load(&periodic_task_error_exit_requested))){

		// Accept new connection

		struct sockaddr_in client_addr; // structure to save client data

		socklen_t addrlen = sizeof(client_addr);

        syslog(LOG_DEBUG, "Waiting for client connection");

		int client_fd = accept(socket_fd, (struct sockaddr *)&client_addr, &addrlen); // Don't care about client's ip and port

        syslog(LOG_DEBUG, "DEBUG: accept returned, client_fd=%d", client_fd);

		if(client_fd == -1){

			print_syscall_error("Could not accept connection: %s", errno);

            continue;

		}



        // Some client connected

        

        char ip[INET_ADDRSTRLEN];

        const char *inet_ret = inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip)); // Get ip   

        if(inet_ret == NULL){

            print_syscall_error("Could not get ip for connection: %s", errno);

        }



        // Clean up closed/error threads

        clean_up_closed_client_threads();



        start_client_thread(client_fd, ip);



        syslog(LOG_DEBUG, "Accepted connection from %s", ip);

	}







    // Error/ signal handling

    // Cleanup everything in reverse order of setup

    

    clean_up_all_client_threads();



    #if(USE_AESD_CHAR_DEVICE != 1)

    if(atomic_load(&periodic_task_error_exit_requested) == 0){

        // Exit not requested by thread

        // Otherwise it would have exited itself already

        if(pthread_kill(timestamp_thread, SIGUSR1) != 0){

            print_syscall_error("Could not kill time_stamp thread: %s", errno);

        }

    }

    if(pthread_join(timestamp_thread, NULL) != 0){// Don't care about return value

        print_syscall_error("Could not join time_stamp thread: %s", errno);

    }

    #endif



    FAIL_SOCKET:

    if(close(socket_fd) == -1){

        print_syscall_error("socket_fd close failed: %s", errno);

    }



    ERROR: exit(-1);



}



#if(USE_AESD_CHAR_DEVICE != 1)

static void timestamp_thread_wake_handler(int signo)

{

    (void)signo;

    // Do nothing, just want to wake up thread from sleep

    // to allow exit gracefully

}





static void *periodic_thread(void *arg)

{

    (void)arg;



    struct timespec next;



    atomic_store(&periodic_task_error_exit_requested, false);



    // Get current monotonic time

    int ret = clock_gettime(CLOCK_MONOTONIC, &next);

    if (ret == -1) {

        // real error

        if(ret == EINTR){

            // Signal arrived to kill process

        }

        else{

            // Some other error

            atomic_store(&periodic_task_error_exit_requested, true);

        }

        print_syscall_error("clock_gettime failed", errno);

        return NULL;

    }



    while (1)

    {

        // -------- periodic work --------

        char time_str[128];



        time_t now = time(NULL);



        struct tm tm_now;



        if (localtime_r(&now, &tm_now) != NULL) {



            strftime(time_str,

                    sizeof(time_str),

                    "timestamp:%a, %d %b %Y %H:%M:%S %z\n",

                    &tm_now);



            syslog(LOG_DEBUG,

                "%s",

                time_str);



            

            // open output file and save fd

            #if (USE_AESD_CHAR_DEVICE != 1)

            int output_fd = open(output_filename, O_CREAT | O_RDWR | O_APPEND, 0644);   // No need of mutex

            #else

            int output_fd = open(output_filename, O_RDWR); //  Do not create,

                                                        // created by load script

                                                        //  append ignored,

                                                        //      driver handles append

                                                        //      so don't need

            #endif

            

            if(output_fd == -1){

                print_syscall_error("Error in opening output file in timestamp write: %s",

                                    errno);

                atomic_store(&periodic_task_error_exit_requested, true);

                break;

            }

            

            

            // append to file

            if(write_to_file(output_fd, time_str, (size_t)(strlen(time_str))) == -1){

                print_syscall_error("Error in writing in timestamp write: %s",

                        errno);

                atomic_store(&periodic_task_error_exit_requested, true);

                break;

            }



            

            // close fd

            if(close(output_fd) == -1){

                print_syscall_error("Error in closing fd in timestamp write: %s",

                                    errno);

                atomic_store(&periodic_task_error_exit_requested, true);

                break;

            }



        }

        else {

            // real error

            print_syscall_error("localtime_r failed", errno);

            atomic_store(&periodic_task_error_exit_requested, true);

            break;

        }



        // --------------------------------



        // Next wakeup = +10 second

        next.tv_sec += 10;



        ret = clock_nanosleep(CLOCK_MONOTONIC,

                                  TIMER_ABSTIME,

                                  &next,

                                  NULL);

        if (ret != 0)

        {

            if(ret == EINTR){

                // Signal arrived to kill process

                syslog(LOG_DEBUG, "signal arrived in periodic thread to kill process");

                break;

            }   

            else{

                // real error

                syslog(LOG_ERR, "clock_nanosleep failed");

                atomic_store(&periodic_task_error_exit_requested, true);

                break;

            }

        }

    }



    return NULL;

}

#endif



static void clean_up_closed_client_threads(void){ // MUST: Handle all syslogs at process level???

    syslog(LOG_DEBUG, "Cleaning up closed client threads");

    node_t *curr;

    node_t *tmp;



    curr = SLIST_FIRST(&head);



    while (curr != NULL){

        tmp = SLIST_NEXT(curr, entries);



        if( atomic_load(&(curr->client_done)) == true){    // MUST: Make thread error not fatal

            // closed

            

            clean_up_client_thread(curr);

        }



        curr = tmp;

    }

}



static void clean_up_all_client_threads(void){

    syslog(LOG_DEBUG, "Cleaning up all client threads");

    node_t *curr;

    node_t *tmp;



    curr = SLIST_FIRST(&head);



    while (curr != NULL){

        tmp = SLIST_NEXT(curr, entries);



        shutdown(curr->client_fd, SHUT_RDWR);  // To unblock client_thread from recv

        clean_up_client_thread(curr);



        curr = tmp;

    }

}



static void clean_up_client_thread(node_t *client_node){



    // acknowledge thread return

    if(pthread_join(client_node->thread_id, NULL) != 0){// Don't care about return value

        print_syscall_error("Could not join thread: %s", errno);

    }

    

    // Remove thread from list

    SLIST_REMOVE(&head, client_node, node, entries);   // MUST: Make O(1) - use TLIST or use next pointer in SLIST

    

    // free up node memory

    syslog(LOG_DEBUG, "Closed connection from %s", client_node->client_ip);

    free(client_node);

    

}





static void start_client_thread(int client_fd, char *ip){

    syslog(LOG_DEBUG, "Starting client thread");

    // add new client node to list head

    node_t *client_node = malloc(sizeof(*client_node));

    if(client_node == NULL){

        print_syscall_error("Could not malloc node: %s", errno);

        if(close(client_node->client_fd) == -1){

            print_syscall_error("Close failed: %s", errno);

        }

        syslog(LOG_DEBUG, "Closed connection from %s", ip);

        return;

    }



    // save node data

    client_node->client_fd = client_fd;

    atomic_store(&(client_node->client_done), false);



    strcpy(client_node->client_ip, ip);



    SLIST_INSERT_HEAD(&head, client_node, entries);



    // create thread and pass in ll node as argument

    if(pthread_create(&(client_node->thread_id), NULL, process_client, client_node) != 0){

        print_syscall_error("Could not create thread: %s", errno);



        if(close(client_node->client_fd) == -1){

            print_syscall_error("Close failed: %s", errno);

        }

        SLIST_REMOVE(&head, client_node, node, entries);

        free(client_node);

        syslog(LOG_DEBUG, "Closed connection from %s", client_node->client_ip);

        return;

    }





    syslog(LOG_DEBUG, "Client processing thread created for ip:%s", ip);

}







// Assumes client_fd assigned

static void *process_client(void *arg_client_node){

    node_t *client_node = arg_client_node;



    char buffer[SIZE_OF_RAM_BUFFER];

    ssize_t num_bytes_rcvd;

    ssize_t total_bytes_rcvd = 0;

    

    int output_fd = -1;



    while(1){

        num_bytes_rcvd = recv(client_node->client_fd, buffer, sizeof(buffer), 0);



        

        if(num_bytes_rcvd == 0){

            // connection closed

            goto CLOSE_CLIENT;

        }

        

        if(num_bytes_rcvd < 0){

            print_syscall_error("Error in receive: %s", errno);

            goto CLOSE_CLIENT;

        }

        

        // Received some bytes

        //         //---------Test Code------------

        // //			for (ssize_t i = 0; i < num_bytes_rcvd; i++) {

            // //				syslog(LOG_DEBUG, "buffer[%zd] = %d", i, (unsigned char)buffer[i]);

            // //			}	

            //         //---------Test Code------------

        

        // open output file and save fd

        #if (USE_AESD_CHAR_DEVICE != 1)

        output_fd = open(output_filename, O_CREAT | O_RDWR | O_APPEND, 0644);   // No need of mutex

        #else

        output_fd = open(output_filename, O_RDWR); //  Do not create,

                                                    // created by load script

                                                    //  append ignored,

                                                    //      driver handles append

                                                    //      so don't need

        #endif

        

        if(output_fd == -1){

            print_syscall_error("Error in opening output file: %s", errno);

            goto CLOSE_CLIENT;

        }

        

        // file opened



        // check for exact ioctl cmd

        

        struct aesd_seekto seekto;

        int consumed = 0;



        int matched = sscanf(buffer,

            "AESDCHAR_IOCSEEKTO:%u,%u%n",

            &seekto.write_cmd,

            &seekto.write_cmd_offset,

            &consumed

        );



        bool valid = matched == 2 && 

            buffer[consumed] == '\n' &&

            buffer[consumed + 1] == '\0';



        if (valid == 1) {

            // ioctl cmd

            // do ioctl operation

            if(ioctl(output_fd, AESDCHAR_IOCSEEKTO, &seekto) == -1){

                goto CLOSE_FD;

            }

            

            // Send all file data to client

            if(send_file_data_to_client(output_fd, client_node->client_fd) == -1){

                goto CLOSE_FD;

            }

        }

        else{

            // not ioctl cmd

            // append to file

            if(write_to_file(output_fd, buffer, (size_t)num_bytes_rcvd) == -1){

                goto CLOSE_FD;

            }

            

            if(buffer[num_bytes_rcvd - 1] == '\n'){

                // packet complete

                

                // Send all file data to client

                if(send_file_data_to_client(output_fd, client_node->client_fd) == -1){

                    goto CLOSE_FD;

                }

            }

        }



        

        // close fd

        if(close(output_fd) == -1){

            print_syscall_error("output_fd close failed: %s", errno);

            goto CLOSE_CLIENT;

        }

        total_bytes_rcvd += num_bytes_rcvd;

    }



CLOSE_FD:

    if(close(output_fd) == -1){

        print_syscall_error("output_fd close failed: %s", errno);

    }   

CLOSE_CLIENT:

    if(close(client_node->client_fd) == -1){

        print_syscall_error("Close failed: %s", errno);

    }

    atomic_store(&(client_node->client_done), true);    // FUTURE: Create thread_status instead of 0/1

                                                        // main will clean up node

    return NULL;   

    

}



static void daemonize(void){    // MUST: Handle errors?



	pid_t pid;

	

	pid = fork();

	if(pid < 0){

        // child never created

		print_syscall_error("Error while daemon 1st fork: %s", errno);

	}

	if(pid > 0){

		exit(EXIT_SUCCESS); // parent exits	

	}

	

	// pid == 0, this is child

    // child continues



	if(setsid() == -1){

		print_syscall_error("Error in setting session id: %s", errno);

	}

	

	// Fork again to prevent accidental acquire of terminal

	if(pid < 0){

		print_syscall_error("Error while daemon 2nd fork: %s", errno);

	}

	if(pid > 0){

		exit(EXIT_SUCCESS); // 1st child exits	

	}

	

	// Reset file permissions umask

	umask(0);	

	

	int fd = open("/dev/null", O_RDWR);

	if(fd == -1){

		print_syscall_error("Error in opening /dev/null: %s", errno);

	}

	

	

    if (dup2(fd, STDIN_FILENO) == -1 ||

        dup2(fd, STDOUT_FILENO) == -1 ||

        dup2(fd, STDERR_FILENO) == -1) {

        close(fd);

        print_syscall_error("Error in dup2: %s", errno);

    }

	

	if (fd > STDERR_FILENO) {

        close(fd);

    }

    

    syslog(LOG_DEBUG, "Daemonized successfully");

}







// Uses output_fd

static int write_to_file(int output_fd, char *buf, size_t num_bytes_to_write){

	

    size_t total_written = 0;



	while(total_written < num_bytes_to_write){



		ssize_t num_bytes_written = 0;



        pthread_mutex_lock(&output_fd_mutex);

        num_bytes_written = write(output_fd, 

                buf + total_written,

                num_bytes_to_write - total_written);

        pthread_mutex_unlock(&output_fd_mutex);



		if(num_bytes_written <= 0){

			print_syscall_error("Error in writing to file: %s", errno);

            return -1;

		}



        total_written += (size_t)num_bytes_written;

	}	



    // all bytes written

    syslog(LOG_DEBUG, "Bytes written to file successfully");

    return 0;

}





// Uses output_fd, client_fd

static int  send_file_data_to_client(int output_fd, int client_fd){

    #if (USE_AESD_CHAR_DEVICE != 1)

	// Go to beginning of file

    pthread_mutex_lock(&output_fd_mutex);

	off_t status = lseek(output_fd, 0, SEEK_SET);			

    pthread_mutex_unlock(&output_fd_mutex);

	if(status == -1){

		print_syscall_error("Error in file seek: %s", errno);

        return -1;

	}

    #endif



	// write everything to send until all bytes sent

	while(1){



		// Read next set of bytes from file

		char buf[SIZE_OF_RAM_BUFFER];

		ssize_t num_bytes_read = 0;

		

        pthread_mutex_lock(&output_fd_mutex);

        num_bytes_read = read(output_fd, buf, SIZE_OF_RAM_BUFFER);

        pthread_mutex_unlock(&output_fd_mutex);

        

		if(num_bytes_read < 0){

			print_syscall_error("Error in reading from file: %s", errno);

            return -1;

		}

		else if(num_bytes_read == 0){

			// sent all bytes

			syslog(LOG_DEBUG, "Bytes sent to client");

			return 0;

		}





		// Send read bytes to client

		size_t bytes_to_send = (size_t)num_bytes_read;

		ssize_t total_sent = 0;

		while(1){	

			if(bytes_to_send <= 0){

				// sent all bytes

				break;

			}



			ssize_t num_bytes_sent = 0;



            pthread_mutex_lock(&output_fd_mutex);

            num_bytes_sent = send(client_fd, buf + total_sent, bytes_to_send, MSG_NOSIGNAL);

            pthread_mutex_unlock(&output_fd_mutex);

			if(num_bytes_sent <= 0){

				print_syscall_error("Error in sending bytes: %s", errno);

                return -1;

			}

			

			bytes_to_send -= (size_t)num_bytes_sent;

			total_sent += num_bytes_sent;

		}

	}



}





static void signal_handler(int signo){

    (void)signo; // ignore

	syslog(LOG_ERR, "Caught signal, exiting");

	signal_exit_requested = 1;

}







static int setup_signal_handlers(void){    // MUST: Use the setting flag and catch in main loop apporoach to ahdnle signals and

                                            // errors

	struct sigaction sa;

	

	#if(USE_AESD_CHAR_DEVICE != 1)

    // timestamp thread signal handler

	memset(&sa, 0, sizeof(sa));

	sa.sa_handler = timestamp_thread_wake_handler;

	sigemptyset(&sa.sa_mask);

	sa.sa_flags = 0;



	if(sigaction(SIGUSR1, &sa, NULL) == -1){

		print_syscall_error("Error in setting up SIGUSR1 sigaction: %s", errno);

        return -1;

	}

	#endif



    // global signal handlers

	memset(&sa, 0, sizeof(sa));

	sa.sa_handler = signal_handler;

	sigemptyset(&sa.sa_mask);

	sa.sa_flags = 0;

    signal_exit_requested = 0; // After this signal can arrive and can set this flag



	if(sigaction(SIGINT, &sa, NULL) == -1){

		print_syscall_error("Error in setting up SIGINT sigaction: %s", errno);

        return -1;

	}

	

	if(sigaction(SIGTERM, &sa, NULL) == -1){

		print_syscall_error("Error in setting up SIGTERM sigaction: %s", errno);

        return -1;

	}



    return 0;



}



// WARNING: error_msg must be null terminated and

//		must contain placeholder(%s) for displaying errno

static void print_syscall_error(char *error_msg, int error_number){

    syslog(LOG_ERR, error_msg, strerror(error_number));

}

















// Lessons

//   Error Handling should be handled as local as possible

//   or as soon above as possible

//   Not every error should be signaled above and has to shutdown whole system

//   Sometimes a policy to handle error is to just log and move on

//   Let signal etc be handled by normal function flow itself

//         instead of abruptly shutting everything down

