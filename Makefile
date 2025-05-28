VERSION = 1.1.2
CC ?= gcc
CFLAGS ?= -O2 -Wall -std=c99
LDFLAGS ?= -lcurses
PREFIX ?= /usr/local
# TODO -lncursesw to handle unicode
SRCS = jakhex.c memsearch.c

jakhex: $(SRCS)
	$(CC) $(CFLAGS) -DVERSION='"$(VERSION)"' -o $@ $(SRCS) $(LDFLAGS)

install: jakhex
	install -m 755 -D -t $(PREFIX)/bin jakhex
	install -m 755 -D -t $(PREFIX)/share/man/man1 jakhex.1

clean:
	rm -rf jakhex

tidy:
	clang-tidy --extra-arg=-std=c99 --extra-arg=-DVERSION='"tidy"' --config-file=tidy.yml $(SRCS)
