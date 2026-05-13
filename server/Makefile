.PHONY: clean all

CC ?= gcc
#CFLAGS ?= -Wall -Wextra -Wconversion -Wsign-conversion -Werror -pthread
CFLAGS ?= -g -O0 -Wall -Wextra -Wconversion -Wsign-conversion -Werror -pthread 

all: aesdsocket

aesdsocket: aesdsocket.c
	$(CC) $(CFLAGS) ./aesdsocket.c -o ./aesdsocket
clean:
	rm -f ./aesdsocket ./*.o


    


