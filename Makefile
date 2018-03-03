CC = gcc
CFLAGS = -Wall -std=gnu99
DEPS = settings.h
EXEC = server client

compile: $(EXEC)

all: compile

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)
	
server: server.o
	$(CC) -o $@ $^ $(CFLAGS) -lm
	
client: client.o
	$(CC) -o $@ $^ $(CFLAGS) -lncurses

clean:
	rm *.o $(EXEC)
