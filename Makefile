CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic

.PHONY: all tcp udp quic clean help

all: tcp udp

tcp:
	$(MAKE) -C tcp

udp:
	$(MAKE) -C udp

quic:
	$(MAKE) -C quic

clean:
	$(MAKE) -C tcp clean
	$(MAKE) -C udp clean
	rm -f logs/*.log

help:
	@echo "Comandos disponibles:"
	@echo "  make        - Compila modulos TCP y UDP"
	@echo "  make tcp    - Compila solo el modulo TCP"
	@echo "  make udp    - Compila solo el modulo UDP"
	@echo "  make quic   - Compila el modulo bono QUIC"
	@echo "  make clean  - Limpia binarios y logs"