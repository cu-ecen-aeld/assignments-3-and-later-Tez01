.PHONY: clean all

CC ?= $(CROSS_COMPILE)gcc
# CFLAGS ?= -Wall -Wextra -Wconversion -Wsign-conversion -Werror -pthread
CFLAGS ?= -g -O0 -Wall -Wextra -Wconversion -Wsign-conversion -Werror -pthread 
LDFLAGS ?=
all: aesdsocket

USE_AESD_CHAR_DEVICE ?= 1

ifeq ($(USE_AESD_CHAR_DEVICE), 1)
CFLAGS += -DUSE_AESD_CHAR_DEVICE=$(USE_AESD_CHAR_DEVICE)
endif

aesdsocket: aesdsocket.c
	$(CC) $(CFLAGS) $(LDFLAGS) ./aesdsocket.c -o ./aesdsocket
clean:
	rm -f ./aesdsocket ./*.o


    


