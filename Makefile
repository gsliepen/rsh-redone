PROGRAMS = rlogin rlogind rsh rshd
MAN1 = rlogin.1 rsh.1
MAN5 = rhosts.5
MAN8 = rlogind.8 rshd.8
PAM = pam/rlogin pam/rsh

CC ?= gcc
CFLAGS ?= -Wall -g -O2 -pipe
PREFIX ?= /usr
INSTALL ?= install
BINDIR ?= $(PREFIX)/bin
SBINDIR ?= $(PREFIX)/sbin
SHAREDIR ?= $(PREFIX)/share
SYSCONFDIR ?= $(PREFIX)/etc
MANDIR ?= $(SHAREDIR)/man
PAMDIR ?= $(SYSCONFDIR)/pam.d

all: $(PROGRAMS)

rlogin: rlogin.c
	$(CC) $(CFLAGS) -o $@ $<

rlogind: rlogind.c
	$(CC) $(CFLAGS) -lutil -lpam -o $@ $<

rsh: rsh.c
	$(CC) $(CFLAGS) -o $@ $<

rshd: rshd.c
	$(CC) $(CFLAGS) -lpam -o $@ $<

install: install-bin install-man install-pam

install-bin: $(PROGRAMS)
	$(INSTALL) -m 4711 rlogin rsh $(DESTDIR)$(BINDIR)/
	$(INSTALL) rlogind rshd $(DESTDIR)$(SBINDIR)/

install-man: $(MAN1) $(MAN5) $(MAN8)
	$(INSTALL) $(MAN1) $(DESTDIR)$(MANDIR)/man1/
	$(INSTALL) $(MAN5) $(DESTDIR)$(MANDIR)/man5/
	$(INSTALL) $(MAN8) $(DESTDIR)$(MANDIR)/man8/

install-pam: $(PAM)
	$(INSTALL) $(PAM) $(DESTDIR)$(PAMDIR)/

clean:
	rm -f $(PROGRAMS)
