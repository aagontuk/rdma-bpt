# Makefile
CC      := gcc
CFLAGS  := -g -O2
LDLIBS  := -lrdmacm -libverbs -lpthread

all: server client

server: server.c
	$(CC) $(CFLAGS) -o $@ server.c $(LDLIBS)

client: client.c
	$(CC) $(CFLAGS) -o $@ client.c $(LDLIBS)

clean:
	rm -f server client
