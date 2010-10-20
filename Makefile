all: daemon-manager dmctl

daemon-manager: daemon-manager.o user.o strprintf.o permissions.o config.cc passwd.o daemon.o log.o

dmctl daemon-manager: CC=g++
dmctl daemon-manager: CXXFLAGS += -MMD -g -Wall -Wextra -Wno-parentheses
dmctl daemon-manager: LDFLAGS  += -g

dmctl: dmctl.o user.o strprintf.o permissions.o passwd.o

clean:
	rm *.o *.d daemon-manager dmctl

-include *.d
