# This Makefile was tested with GNU Make
CC=gcc
LD=gcc

# Use pkg-config to lookup the proper compiler and linker flags for LCM
CFLAGS=-I/usr/local/include -I/usr/include -Wall -g -O2 -DPARALLEL=32 -DNDEBUG=1 -fPIC -std=gnu99
LDFLAGS=stream_tools.o -L/usr/local/lib64 -L/usr/local/lib -lstdc++ -lzmq -lczmq -lm -lpthread -g

TARGETS= stream_router stream_test_source test_subscriber

.PRECIOUS: %.o	

.PHONY: all
all: stream_tools.o $(TARGETS)

%.c:

%: %.c

%: %.o
	${LD} -o $@ $< ${LDFLAGS}

# If stream_tools.h changes then recompile
%.o: %.c stream_tools.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGETS)
	rm -f *.o
	rm -f exlcm_example_t.c exlcm_example_t.h
