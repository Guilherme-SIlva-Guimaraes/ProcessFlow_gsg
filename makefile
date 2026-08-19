CC = gcc
CFLAGS = -Wall -Wextra -g

all: processflow

processflow: main.c
	$(CC) $(CFLAGS) main.c -o processflow

run: processflow
	./processflow

clean:
	rm -f processflow

.PHONY: all run clean
