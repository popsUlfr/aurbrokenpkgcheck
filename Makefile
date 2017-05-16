CC=gcc
CFLAGS=-Wall -Wextra -pedantic -Wconversion
LDFLAGS=-Wl,-O1,--sort-common,--as-needed,-z,relro
DEBUG_CFLAGS=-g
CLANG_CFLAGS=-Weverything -Wno-objc-missing-property-synthesis
INCLUDES= $(shell pkg-config --cflags libalpm)
LIBS= $(shell pkg-config --libs libalpm)

.PHONY: all aurbrokenpkgcheck aurbrokenpkgcheck_debug clean valgrind static-analysis

all: aurbrokenpkgcheck

aurbrokenpkgcheck:
	$(CC) $(CFLAGS) $(LDFLAGS) $(INCLUDES) aurbrokenpkgcheck.c -o aurbrokenpkgcheck $(LIBS)
	
aurbrokenpkgcheck_debug:
	$(CC) $(CFLAGS) $(DEBUG_CFLAGS) $(LDFLAGS) $(INCLUDES) aurbrokenpkgcheck.c -o aurbrokenpkgcheck_debug $(LIBS)

clean:
	rm -f aurbrokenpkgcheck aurbrokenpkgcheck_debug
	
valgrind: aurbrokenpkgcheck_debug
	valgrind --trace-children=no --track-fds=yes --leak-check=full --show-leak-kinds=all ./aurbrokenpkgcheck_debug
	
static-analysis:
	scan-build -v -v -v -V make aurbrokenpkgcheck_debug
