.PHONY: clean all

CC ?= gcc
CFLAGS ?= -Wall -Wextra -Wconversion -Wsign-conversion -Werror


all: aesdsocket

aesdsocket: aesdsocket.c
	$(CC) $(CFLAGS) ./aesdsocket.c -o ./aesdsocket
clean:
	rm -f ./aesdsocket ./*.o


    


