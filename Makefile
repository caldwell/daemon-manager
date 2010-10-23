EXE=daemon-manager dmctl
all: $(EXE)

daemon-manager: daemon-manager.o user.o strprintf.o permissions.o config.cc passwd.o daemon.o log.o options.o

dmctl daemon-manager: CC=g++
dmctl daemon-manager: CXXFLAGS += -MMD -g -Wall -Wextra -Wno-parentheses
dmctl daemon-manager: LDFLAGS  += -g

dmctl: dmctl.o user.o strprintf.o permissions.o passwd.o options.o

-include *.d

clean:
	rm -f *.o *.d $(EXE) $(MAN)

MAN=dmctl.1 daemon-manager.1 daemon.conf.5 daemon-manager.conf.5
all: $(MAN)

PODFLAGS=--release=daemon-manager-0.1 --center="Daemon Manager Documentation"

%.1 : %.cc
	pod2man $(PODFLAGS) $< $@

%.5 : %.pod
	pod2man $(PODFLAGS) $< $@
