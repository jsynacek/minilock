CC = cc
CFLAGS = -std=c99 -pedantic -Wall
LDFLAGS = -lcrypt -lX11 -lXrandr
PREFIX ?= /usr/local

all: minilock

minilock: minilock.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o minilock minilock.c

install: all
	@echo installing minilock to $(DESTDIR)$(PREFIX)/bin/minilock
	@mkdir -p $(DESTDIR)$(PREFIX)/bin
	@cp -f minilock $(DESTDIR)$(PREFIX)/bin/minilock
	@chmod u=rwxs,g=rx,o=rx $(DESTDIR)$(PREFIX)/bin/minilock

uninstall:
	@echo uninstalling $(DESTDIR)$(PREFIX)/bin/minilock
	@rm -f $(DESTDIR)$(PREFIX)/bin/minilock

clean:
	rm -f minilock

.PHONY: all minilock install uninstall clean
