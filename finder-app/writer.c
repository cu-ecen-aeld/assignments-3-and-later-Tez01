#include <syslog.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#define STR_MAX_LEN	50


int main(int argc, char *argv[]){

	openlog("writer", LOG_PID, LOG_USER);

	if(argc != 3){
		syslog(LOG_ERR, "There must be exactly 2 arguments: writefile and writestr");
		exit(1);
	}
		
	// open file
	int file_descriptor = open(argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0744);

	if(file_descriptor == 1){
		 syslog(LOG_ERR, "Could not create file");
		 exit(1);
	}

	// File opened

	// write to file
	syslog(LOG_DEBUG, "Writing %s to %s", argv[2], argv[1]);
	ssize_t n = write(file_descriptor, argv[2], strlen(argv[2]));

	if((n <= 0) || (n != strlen(argv[2]))){
		syslog(LOG_ERR, "Write error");
		exit(1);
	}

	exit(0);

}
