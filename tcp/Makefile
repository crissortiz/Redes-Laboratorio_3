CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic

all: broker_tcp publisher_tcp subscriber_tcp

broker_tcp: broker_tcp.c
	$(CC) $(CFLAGS) -o broker_tcp broker_tcp.c

publisher_tcp: publisher_tcp.c
	$(CC) $(CFLAGS) -o publisher_tcp publisher_tcp.c

subscriber_tcp: subscriber_tcp.c
	$(CC) $(CFLAGS) -o subscriber_tcp subscriber_tcp.c

clean:
	rm -f broker_tcp publisher_tcp subscriber_tcp
