.PHONY: clean

COMPILER = $(CROSS_COMPILE)gcc

all: writer

writer: writer.c
	$(COMPILER) writer.c -o writer

clean:
	rm -f ./writer ./*.o
