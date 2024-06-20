VERSION = 1.0.0
CC ?= gcc
#CFLAGS ?= -O2 -std=c99
CFLAGS ?= -g -std=c99
LDFLAGS ?= -lcurses
PREFIX ?= /usr/local
# TODO -lncursesw to handle unicode
SRCS = jakhex.c memsearch.c

jakhex: $(SRCS)
	$(CC) $(CFLAGS) -DVERSION='"$(VERSION)"' -o $@ $(SRCS) $(LDFLAGS)

install: jakhex
	install -m 755 -D -t $(PREFIX)/bin jakhex

clean:
	rm -rf jakhex