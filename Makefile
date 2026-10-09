CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic

all: tcp udp

tcp: broker_tcp publisher_tcp subscriber_tcp
udp: broker_udp publisher_udp subscriber_udp

broker_tcp: broker_tcp.c
	$(CC) $(CFLAGS) -o broker_tcp broker_tcp.c

publisher_tcp: publisher_tcp.c
	$(CC) $(CFLAGS) -o publisher_tcp publisher_tcp.c

subscriber_tcp: subscriber_tcp.c
	$(CC) $(CFLAGS) -o subscriber_tcp subscriber_tcp.c

broker_udp: broker_udp.c
	$(CC) $(CFLAGS) -o broker_udp broker_udp.c

publisher_udp: publisher_udp.c
	$(CC) $(CFLAGS) -o publisher_udp publisher_udp.c

subscriber_udp: subscriber_udp.c
	$(CC) $(CFLAGS) -o subscriber_udp subscriber_udp.c

clean:
	rm -f broker_tcp publisher_tcp subscriber_tcp broker_udp publisher_udp subscriber_udp *.o *.log