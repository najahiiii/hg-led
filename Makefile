CC = cc
CFLAGS = -Wall -Wextra -O3
TARGETS = trafmon hgledon
DEPS = hgledon.h

SRCS_trafmon = trafmon.c hgledon.c
SRCS_hgledon = main.c hgledon.c

OBJS_trafmon = $(SRCS_trafmon:.c=.o)
OBJS_hgledon = $(SRCS_hgledon:.c=.o)

all: $(TARGETS)

PREFIX = /usr/bin

trafmon: $(OBJS_trafmon)
		$(CC) $(CFLAGS) -o trafmon $(OBJS_trafmon)

hgledon: $(OBJS_hgledon)
		$(CC) $(CFLAGS) -o hgledon $(OBJS_hgledon)

%.o: %.c $(DEPS)
		$(CC) $(CFLAGS) -c $< -o $@

install: all
		cp trafmon $(PREFIX)/
		cp hgledon $(PREFIX)/
		chmod 755 $(PREFIX)/trafmon $(PREFIX)/hgledon

uninstall:
		rm -f $(PREFIX)/trafmon $(PREFIX)/hgledon

clean:
		rm -f $(TARGETS) *.o
