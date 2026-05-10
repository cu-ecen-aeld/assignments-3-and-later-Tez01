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

#define SIZE_OF_RAM_BUFFER	1000

static int socket_fd = 0;
static int normal_fd = 0;
pthread_mutex_t normal_fd_mutex;

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


//------------------------------------------
// Static function prototypes
//------------------------------------------
static void daemonize(void);

static void *process_client(void *client_node);

static void clean_up_closed_threads(void);
static void start_client_thread(int client_fd, char *ip);

static void write_to_file(char *buf, ssize_t num_bytes_to_write);
static void send_file_data_to_client(int client_fd);

static void signal_handler(int signo);
static void setup_signal_handlers(void);

static void error_handler(char *error_msg, int error_number);
static void clean_up(void);

static char output_filename[] = "/var/tmp/aesdsocketdata";

int main(int argc, char *argv[]){
	socket_fd = -1;
	normal_fd = -1;

	// Enable logging
	openlog("aesdsocket", LOG_PID, LOG_DAEMON);
	
	// Register signal handlers
	setup_signal_handlers();
	
	// create a socket
	socket_fd = socket(AF_INET, SOCK_STREAM, 0);
	if(socket_fd == -1){
		error_handler("Could not create socket: %s", errno);
	}

	int opt = 1;	
	setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	// bind socket
	int status;
	struct addrinfo hints;
	struct addrinfo *servinfo; // will point to the results
	
	memset(&hints, 0, sizeof(hints)); // make sure struct is empty
	hints.ai_family = AF_UNSPEC; // Don't care IPv4 or IPv6
	hints.ai_socktype = SOCK_STREAM;	// TCP stream sockets
	hints.ai_flags = AI_PASSIVE;	// fill in my IP for me

	if((status = getaddrinfo(NULL, "9000", &hints, &servinfo)) != 0){
		error_handler("Could not getaddrinfo: %s", errno);
	}
	
	// servinfo now points to a linked list of 1 or more struct addrinfos

	if((status = bind(socket_fd, servinfo->ai_addr, servinfo->ai_addrlen)) != 0){
		error_handler("Could not bind: %s", errno);
	}
	
	freeaddrinfo(servinfo);	// no use of servinfo now, free

	if(argc == 2){
		if(strcmp(argv[1], "-d") == 0){
			daemonize();
		}
	}
	
	// Listen for new connections
	if((status = listen(socket_fd, 5)) != 0){
		error_handler("Could not listen: %s", errno);
	}

    // Initialize linked list to store threads
    SLIST_INIT(&head);  // MUST: Handle return values/errors of Linked List macros

    pthread_mutex_init(&normal_fd_mutex, NULL);
    
	while(1){
		// Accept new connection
		struct sockaddr_in client_addr; // structure to save client data
		socklen_t addrlen = sizeof(client_addr);
		int client_fd = accept(socket_fd, (struct sockaddr *)&client_addr, &addrlen); // Don't care about client's ip and port
		if(client_fd == -1){
			error_handler("Could not accept connection: %s", errno);
		}

        // Some client connected
        
        char ip[INET_ADDRSTRLEN];
        const char *inet_ret = inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip)); // Get ip   
        if(inet_ret == NULL){
            error_handler("Could not get ip for connection: %s", errno);
        }

        // Clean up closed/error threads
        clean_up_closed_threads();

        start_client_thread(client_fd, ip);

        syslog(LOG_DEBUG, "Accepted connection from %s", ip);
	}
}


static void clean_up_closed_threads(void){ // MUST: Handle all syslogs at process level???
    node_t *curr;
    node_t *tmp;

    curr = SLIST_FIRST(&head);

    while (curr != NULL){
        tmp = SLIST_NEXT(curr, entries);

        if( atomic_load(&(curr->client_done)) == 0){    // MUST: Make thread error not fatal
            // closed
            // thread local cleanup ensures mutex released
    
            
            // acknowledge thread return
            if(pthread_join(curr->thread_id, NULL) != 0){// Don't care about return value
                error_handler("Could not join thread: %s", errno);
            }
            
            // Remove thread from list
            SLIST_REMOVE(&head, curr, node, entries);   // MUST: Make O(1) - use TLIST or use next pointer in SLIST
            
            // free up node memory
            free(curr);
            
            syslog(LOG_DEBUG, "Closed connection from %s", curr->client_ip);
        }

        curr = tmp;
    }
}


static void start_client_thread(int client_fd, char *ip){
    // add new client node to list head
    node_t *client_node = malloc(sizeof(*client_node));
    if(client_node == NULL){
        error_handler("Could not malloc node: %s", errno);
    }

    // save node data
    client_node->client_fd = client_fd;
    atomic_store(&(client_node->client_done), false);

    strcpy(client_node->client_ip, ip);

    SLIST_INSERT_HEAD(&head, client_node, entries);

    // create thread and pass in ll node as argument
    if(pthread_create(&(client_node->thread_id), NULL, process_client, client_node) != 0){
        error_handler("Could not create thread: %s", errno);
    }


    syslog(LOG_DEBUG, "Client processing thread created for ip:%s", ip);
}


// Assumes client_fd assigned
static void *process_client(void *arg_client_node){
    node_t *client_node = arg_client_node;

    char buffer[SIZE_OF_RAM_BUFFER];
    ssize_t num_bytes_rcvd;
    ssize_t total_bytes_rcvd = 0;
    normal_fd = open(output_filename, O_CREAT | O_RDWR | O_APPEND, 0644);
    if(normal_fd == -1){
        error_handler("Error in opening file: %s", errno);
    }
    
    while(1){
        num_bytes_rcvd = recv(client_node->client_fd, buffer, sizeof(buffer), 0);

        if(num_bytes_rcvd == 0){
            atomic_store(&(client_node->client_done), true);
                                        // main will clean up and close client connection
            break;
        }
        
        
        if(num_bytes_rcvd < 0){
            error_handler("Error in receive: %s", errno);
        }
        
        // Received some bytes
        
        
//         //---------Test Code------------
// //			for (ssize_t i = 0; i < num_bytes_rcvd; i++) {
// //				syslog(LOG_DEBUG, "buffer[%zd] = %d", i, (unsigned char)buffer[i]);
// //			}	
//         //---------Test Code------------
        
        total_bytes_rcvd += num_bytes_rcvd;

        // append to file
        write_to_file(buffer, num_bytes_rcvd);


        if(buffer[num_bytes_rcvd - 1] == '\n'){
            // packet complete
            
            // Send to client
            // go to beginning of file
            send_file_data_to_client(client_node->client_fd);


        }
    }

    return NULL;
}

static void daemonize(void){

	pid_t pid;
	
	pid = fork();
	
	if(pid < 0){
		error_handler("Error while daemon 1st fork: %s", errno);
	}
	if(pid > 0){
		exit(EXIT_SUCCESS); // parent exits	
	}
	
	
	if(setsid() == -1){
		error_handler("Error in setting session id: %s", errno);
	}
	
	// Fork again to prevent accidental acquire of terminal
	if(pid < 0){
		error_handler("Error while daemon 2nd fork: %s", errno);
	}
	if(pid > 0){
		exit(EXIT_SUCCESS); // 1st child exits	
	}
	
	// Reset file permissions umask
	umask(0);	
	
	int fd = open("/dev/null", O_RDWR);
	if(fd == -1){
		error_handler("Error in opening /dev/null: %s", errno);
	}
	
	
    if (dup2(fd, STDIN_FILENO) == -1 ||
        dup2(fd, STDOUT_FILENO) == -1 ||
        dup2(fd, STDERR_FILENO) == -1) {
        close(fd);
        error_handler("Error in dup2: %s", errno);
    }
	
	if (fd > STDERR_FILENO) {
        close(fd);
    }
    
    syslog(LOG_DEBUG, "Daemonized successfully");
}



// Uses normal_fd
static void write_to_file(char *buf, ssize_t num_bytes_to_write){
		
	while(1){
		if(num_bytes_to_write <= 0){
			// all bytes written
			syslog(LOG_DEBUG, "Bytes written to file successfully");
			return;
		}

		ssize_t num_bytes_written = 0;

        pthread_mutex_lock(&normal_fd_mutex);
        num_bytes_written = write(normal_fd, buf, (size_t)num_bytes_to_write);
        pthread_mutex_unlock(&normal_fd_mutex);

		if(num_bytes_written <= 0){
			error_handler("Error in writing to file: %s", errno);
		}

		num_bytes_to_write -= num_bytes_written;
	}	
}


// Uses normal_fd, client_fd
static void send_file_data_to_client(int client_fd){
	// Go to beginning of file
    pthread_mutex_lock(&normal_fd_mutex);
	off_t status = lseek(normal_fd, 0, SEEK_SET);			
    pthread_mutex_unlock(&normal_fd_mutex);

	if(status == -1){
		error_handler("Error in file seek: %s", errno);
	}


	// write everything to send until all bytes sent
	while(1){

		// Read next set of bytes from file
		char buf[SIZE_OF_RAM_BUFFER];
		ssize_t num_bytes_read = 0;
		
        pthread_mutex_lock(&normal_fd_mutex);
        num_bytes_read = read(normal_fd, buf, SIZE_OF_RAM_BUFFER);
        pthread_mutex_unlock(&normal_fd_mutex);
        
		if(num_bytes_read < 0){
			error_handler("Error in reading from file: %s", errno);
		}
		else if(num_bytes_read == 0){
			// sent all bytes
			syslog(LOG_DEBUG, "Bytes sent to client");
			return;
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

            pthread_mutex_lock(&normal_fd_mutex);
            num_bytes_sent = send(client_fd, buf + total_sent, bytes_to_send, MSG_NOSIGNAL);
            pthread_mutex_unlock(&normal_fd_mutex);
			if(num_bytes_sent <= 0){
			
				error_handler("Error in sending bytes: %s", errno);
			}
			
			bytes_to_send -= (size_t)num_bytes_sent;
			total_sent += num_bytes_sent;
		}
	}

}


static void signal_handler(int signo){
    (void)signo; // ignore
	syslog(LOG_ERR, "Caught signal, exiting");
	clean_up();
	exit(-1);
}



static void setup_signal_handlers(void){    // MUST: Use the setting flag and catch in main loop apporoach to ahdnle signals and
                                            // errors
	struct sigaction sa;
	
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = signal_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	
	if(sigaction(SIGINT, &sa, NULL) == -1){
		error_handler("Error in setting up SIGINT sigaction: %s", errno);
	}
	
	if(sigaction(SIGTERM, &sa, NULL) == -1){
		error_handler("Error in setting up SIGTERM sigaction: %s", errno);
	}

}


// WARNING: error_msg must be null terminated and
//		must contain placeholder(%s) for displaying errno
static void error_handler(char *error_msg, int error_number){
	
	syslog(LOG_ERR, error_msg, strerror(error_number));
	clean_up();
	exit(-1);
}


// Uses normal_fd, client_fd, socket_fd
static void clean_up(void){

	if(normal_fd != -1){	
		if(close(normal_fd) == -1){
			syslog(LOG_ERR, "normal_fd close failed: %s", strerror(errno));
		}
	}
	
	if ((remove(output_filename) == -1) && (errno != ENOENT)) {
		syslog(LOG_ERR, "remove failed: %s", strerror(errno));
	}
	
	if(socket_fd != -1){
		if(close(socket_fd) == -1){
			syslog(LOG_ERR, "socket_fd close failed: %s", strerror(errno));
		}
	}

    // Clear linked list
    // Close any open connections
    // node_t *curr;        // MUST: Implement

    // while(!SLIST_EMPTY(&head)){
    //     curr = SLIST_FIRST(&head);

    //     SLIST_REMOVE_HEAD(&head, entries);

    //     if(curr->)
    // }
	// if(client_fd != -1){
	// 	if(close(client_fd) == -1){
	// 		syslog(LOG_ERR, "client_fd close failed: %s", strerror(errno));
	// 	}
	// }

}






