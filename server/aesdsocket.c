#include <fcntl.h>

#include <syslog.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <errno.h>

#include <string.h>

#include <netdb.h>

#include <stdlib.h>

#include <arpa/inet.h>

#include <unistd.h>

#include <signal.h>

#include <stdio.h>

#define SIZE_OF_RAM_BUFFER	1000

static int socket_fd = 0;
static int client_fd = 0;
static int normal_fd = 0;


static void write_to_file(char *buf, size_t num_bytes_to_write);
static void send_file_data_to_client(size_t num_bytes_to_send);

static void signal_handler(int signo);
static void setup_signal_handlers(void);

static void error_handler(char *error_msg, int error_number);
static void clean_up(void);

static char output_filename[] = "/var/tmp/aesdsocketdata";

int main(){
	socket_fd = -1;
	client_fd = -1;
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

	int opt = 1;	// MUST: Remove
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
	
	free(servinfo);	// no use of servinfo now, free

	// Listen for new connections
	if((status = listen(socket_fd, 5) != 0)){
		error_handler("Could not listen: %s", errno);
	}

	while(1){
		// Accept new connection
		struct sockaddr_in client_addr; // structure to save client data
		socklen_t addrlen = sizeof(client_addr);
		client_fd = accept(socket_fd, (struct sockaddr *)&client_addr, &addrlen); // Don't care about client's ip and port
		if(client_fd == -1){
			error_handler("Could not accept connection: %s", errno);
		}

		// Client connected	
		char ip[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));

		syslog(LOG_DEBUG, "Accepted connection from %s", ip);


		char buffer[SIZE_OF_RAM_BUFFER];
		ssize_t num_bytes_rcvd;
		ssize_t total_bytes_rcvd = 0;
		normal_fd = open(output_filename, O_CREAT | O_RDWR | O_APPEND, 0644);
		if(normal_fd == -1){
			error_handler("Error in opening file: %s", errno);
		}
		
		while(1){
			num_bytes_rcvd = recv(client_fd, buffer, sizeof(buffer), 0);

			if(num_bytes_rcvd == 0){
				// MUST: Close socket and file wherever error occuring???
				if(close(client_fd) == -1){
					error_handler("Close failed: %s", errno);
				}

				syslog(LOG_DEBUG, "Closed connection from %s", ip);
				break;
			}
			
			
			if(num_bytes_rcvd < 0){
				error_handler("Error in receive: %s", errno);
			}
			
			// Received some bytes
			for (ssize_t i = 0; i < num_bytes_rcvd; i++) {// MUST: Remove
				syslog(LOG_DEBUG, "buffer[%zd] = %d", i, (unsigned char)buffer[i]);
			}	

			total_bytes_rcvd += num_bytes_rcvd;

			// append to file
			write_to_file(buffer, num_bytes_rcvd);


			if(buffer[num_bytes_rcvd - 1] == '\n'){
				// packet complete
				
				// Send to client
				// go to beginning of file
				send_file_data_to_client(total_bytes_rcvd);


			}
		}
	}
}


// Uses normal_fd
static void write_to_file(char *buf, size_t num_bytes_to_write){
		
	while(1){
		if(num_bytes_to_write <= 0){
			// all bytes written
			syslog(LOG_DEBUG, "Bytes written to file successfully");
			return;
		}

		ssize_t num_bytes_written = 0;
		if((num_bytes_written = write(normal_fd, buf, num_bytes_to_write)) <= 0){
			error_handler("Error in writing to file: %s", errno);
		}

		num_bytes_to_write -= num_bytes_written;

	}	
}


// Uses normal_fd, client_fd
static void send_file_data_to_client(size_t num_bytes_to_send){
	// Go to beginning of file
	int status = lseek(normal_fd, 0, SEEK_SET);			

	if(status == -1){
		error_handler("Error in file seek: %s", errno);
	}


	// write everything to send until all bytes sent
	while(1){
		if(num_bytes_to_send <= 0){
			// sent all bytes
			 syslog(LOG_DEBUG, "Bytes sent to client");

			return;
		}

		// Read next set of bytes from file
		char buf[SIZE_OF_RAM_BUFFER];
		ssize_t num_bytes_read = 0;
		
		if((num_bytes_read = read(normal_fd, buf, SIZE_OF_RAM_BUFFER)) <= 0){
			error_handler("Error in reading from file: %s", errno);
		}


		// Send read bytes to client
		size_t bytes_to_send = num_bytes_read;
		ssize_t total_sent = 0;
		while(1){	
			if(bytes_to_send <= 0){
				// sent all bytes
				break;
			}

			ssize_t num_bytes_sent = 0;

			if((num_bytes_sent = send(client_fd, buf + total_sent, bytes_to_send, MSG_NOSIGNAL)) <= 0){	// MUST: Set proper flags
			
				error_handler("Error in sending bytes: %s", errno);
			}
			
			bytes_to_send -= num_bytes_sent;
			total_sent += num_bytes_sent;
		}


		num_bytes_to_send -= num_bytes_read;
	}

}


static void signal_handler(int signo){
	syslog(LOG_ERR, "Caught signal, exiting");
	clean_up();
	exit(-1);
}



static void setup_signal_handlers(void){
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
	
	if (remove(output_filename) == -1) {
		syslog(LOG_ERR, "remove failed: %s", strerror(errno));
	}
	
	if(socket_fd != -1){
		if(close(socket_fd) == -1){
			syslog(LOG_ERR, "socket_fd close failed: %s", strerror(errno));
		}
	}
	if(client_fd != -1){
		if(close(client_fd) == -1){
			syslog(LOG_ERR, "client_fd close failed: %s", strerror(errno));
		}
	}

}






