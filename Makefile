PROGRAMS = rlogin rlogind rsh rshd

CC ?= gcc
CFLAGS ?= -Wall -g -O2 -pipe
PREFIX ?= /usr
INSTALL ?= install
BINDIR = $(PREFIX)/bin
SBINDIR = $(PREFIX)/sbin

all: $(PROGRAMS)

rlogin: rlogin.c
	$(CC) $(CFLAGS) -o $@ $<

rlogind: rlogind.c
	$(CC) $(CFLAGS) -lutil -lpam -o $@ $<

rsh: rsh.c
	$(CC) $(CFLAGS) -o $@ $<

rshd: rshd.c
	$(CC) $(CFLAGS) -lpam -o $@ $<

install: $(PROGRAMS)
	$(INSTALL) -m 4711 rlogin rsh $(DESTDIR)$(BINDIR)/
	$(INSTALL) rlogind rshd $(DESTDIR)$(SBINDIR)/

clean:
	rm -f $(PROGRAMS)
