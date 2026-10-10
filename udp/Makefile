CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic

all: broker_udp publisher_udp subscriber_udp

broker_udp: broker_udp.c
	$(CC) $(CFLAGS) -o broker_udp broker_udp.c

publisher_udp: publisher_udp.c
	$(CC) $(CFLAGS) -o publisher_udp publisher_udp.c

subscriber_udp: subscriber_udp.c
	$(CC) $(CFLAGS) -o subscriber_udp subscriber_udp.c

clean:
	rm -f broker_udp publisher_udp subscriber_udp
