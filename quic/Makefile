PQ ?= ../picoquic
BUILD = $(PQ)/build
PTLS  = $(BUILD)/_deps/picotls-build

CC     = gcc
CFLAGS = -Wall -O2 -I$(PQ)/picoquic -I$(BUILD)/_deps/picotls-src/include
LDFLAGS =

OPENSSL_PREFIX := $(shell brew --prefix openssl@3 2>/dev/null)
ifneq ($(OPENSSL_PREFIX),)
  CFLAGS  += -I$(OPENSSL_PREFIX)/include
  LDFLAGS += -L$(OPENSSL_PREFIX)/lib
endif

LIBS = $(BUILD)/libpicoquic-core.a \
       $(PTLS)/libpicotls-openssl.a \
       $(wildcard $(PTLS)/libpicotls-fusion.a) \
       $(PTLS)/libpicotls-minicrypto.a \
       $(PTLS)/libpicotls-core.a \
       $(LDFLAGS) -lssl -lcrypto -lpthread -lm

all: broker_quic publisher_quic subscriber_quic

%_quic: %_quic.c
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

cert.pem key.pem:
	openssl req -x509 -newkey rsa:2048 -nodes -keyout key.pem -out cert.pem \
	    -days 365 -subj "/CN=localhost"

clean:
	rm -f broker_quic publisher_quic subscriber_quic