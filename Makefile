CC = cc
CFLAGS = -Wall -Wextra -O3

PREFIX ?= /usr/bin

TARGETS = hgledon trafmon

SRCS_hgledon = hgledon.c
SRCS_trafmon = trafmon.c hgledon.c

all: $(TARGETS)

trafmon: $(SRCS_trafmon)
	$(CC) $(CFLAGS) -o $@ $^

hgledon: $(SRCS_hgledon)
	$(CC) $(CFLAGS) -DHGLEDON_MAIN -o $@ $^

install: all
	cp trafmon $(PREFIX)/trafmon
	cp hgledon $(PREFIX)/hgledon

uninstall:
	rm -f $(PREFIX)/trafmon $(PREFIX)/hgledon

clean:
	rm -f $(TARGETS)

debug: CFLAGS += -g -DDEBUG
debug: clean all

.PHONY: all install uninstall clean debug
