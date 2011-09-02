CC=gcc
CFLAGS=-g -O3
LDFLAGS=
STRIP=strip
DESTDIR=
PREFIX=/usr

OBJS=genfd.o mudem.o muxsocket.o muxstdio.o tcp4.o unix.o

all: umlbox-mudem

umlbox-mudem: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) -o umlbox-mudem

.SUFFIXES: .c .o

%.o: %.c
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f $(OBJS) umlbox-mudem
	rm -f deps

mrproper: clean
	-true

install:
	install -D umlbox-mudem $(DESTDIR)$(PREFIX)/bin/umlbox-mudem


include deps

deps: *.c *.h
	$(CC) -MM *.c > deps
