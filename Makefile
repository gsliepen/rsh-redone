PROGRAMS = rlogin rlogind rsh

CC ?= gcc
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

install: $(PROGRAMS)
	$(INSTALL) rlogin rsh $(DESTDIR)$(BINDIR)/
	$(INSTALL) -m 4711 rlogind $(DESTDIR)$(SBINDIR)/

clean:
	rm -f $(PROGRAMS)
