VERSION:=$(shell sed -E '1 s/^[-_a-zA-Z0-9]+ \((.*)\)/\1/;q' Changes)

SBIN=daemon-manager
BIN=dmctl
all: bin
bin: $(SBIN) $(BIN)

daemon-manager: daemon-manager.o user.o strprintf.o permissions.o config.o passwd.o daemon.o log.o options.o posix-util.o json-escape.o command-sock.o peercred.o

COMMAND_SOCKET_PATH ?= "/var/run/daemon-manager.sock"

dmctl daemon-manager: CC=g++
dmctl daemon-manager: CXXFLAGS += -std=c++11 -MMD -g -Wall -Wextra -Wno-parentheses
dmctl daemon-manager: CPPFLAGS += -DVERSION=\"$(VERSION)\" -DCOMMAND_SOCKET_PATH=\"$(COMMAND_SOCKET_PATH)\"
dmctl daemon-manager: LDFLAGS  += -g

dmctl: dmctl.o user.o strprintf.o permissions.o passwd.o options.o posix-util.o command-sock.o

-include *.d

clean:
	rm -f *.o *.d $(SBIN) $(BIN) $(MAN1) $(MAN5)

MAN1=dmctl.1 daemon-manager.1
MAN5=daemon.conf.5 daemon-manager.conf.5

man: $(MAN1) $(MAN5)
all: man

ASCIIDOC_FLAGS=--attribute=revnumber="daemon-manager-$(VERSION)" --attribute=manmanual="Daemon Manager Documentation" --no-xmllint

%.1 : %.cc
	a2x $(ASCIIDOC_FLAGS) -f manpage $<

%.5 : %.asciidoc
	a2x $(ASCIIDOC_FLAGS) -f manpage $<

# Install stuff
DESTDIR    =
PREFIX     = /usr/local
ifeq ($(PREFIX),/usr)
	ETC_DIR  = /etc
else
	ETC_DIR  = $(PREFIX)/etc
endif
SBIN_DIR = $(PREFIX)/sbin
BIN_DIR  = $(PREFIX)/bin
MAN_DIR  = $(PREFIX)/share/man

install: all
	mkdir -p $(DESTDIR)$(SBIN_DIR);     cp -a $(SBIN) $(DESTDIR)$(SBIN_DIR)
	mkdir -p $(DESTDIR)$(BIN_DIR);      cp -a $(BIN)  $(DESTDIR)$(BIN_DIR)
	mkdir -p $(DESTDIR)$(MAN_DIR)/man1; cp -a $(MAN1) $(DESTDIR)$(MAN_DIR)/man1
	mkdir -p $(DESTDIR)$(MAN_DIR)/man5; cp -a $(MAN5) $(DESTDIR)$(MAN_DIR)/man5
	mkdir -p $(DESTDIR)$(ETC_DIR)/daemon-manager/daemons
	install -m 600 -o 0 -g 0 daemon-manager.conf.basic $(DESTDIR)$(ETC_DIR)/daemon-manager/daemon-manager.conf

TAGS: *.c *.h *.cc
	find . -name "*.[ch]" | etags -

# Release stuff (only useful to the maintainer for making releases)
release: daemon-manager-$(VERSION).tar.gz

daemon-manager-$(VERSION).tar.gz: daemon-manager-$(VERSION)
	tar czf $@ $<

daemon-manager-$(VERSION):
	rm -rf $@
	git clone . $@
	cd $@ && git checkout $(VERSION)
	rm -rf $@/.git* $@/.ditz*
	make -C $@ man

update-copyrights:
	year=$$(date +%Y);   \
	last=$$(($$year-1)); \
	sed -i -E "s/(Copyright .* 20..)(|-20..) David/\1-$$year David/" $$(git diff --name-only master@{$$last-01-01} HEAD)

# grab CI stuff, if it's available.
-include .github/ci.make
