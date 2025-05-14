# Makefile
CC      := gcc
CFLAGS  := -g -O2
LDLIBS  := -lrdmacm -libverbs -lpthread

all: server client

server: server.c bpt.c
	$(CC) $(CFLAGS) -o $@ server.c bpt.c $(LDLIBS)

client: client.c
	$(CC) $(CFLAGS) -o $@ client.c $(LDLIBS)

clean:
	rm -f server client
