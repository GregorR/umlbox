CC=gcc
CFLAGS=-O3
LDFLAGS=
STATICFLAGS=-static -DSTATIC_BUILD
STRIP=strip
DESTDIR=
PREFIX=/usr

OBJS=mux.o muxsocket.o muxstdio.o tcp4.o unix.o
SOBJS=$(OBJS:.o=-static.o)

all: umlbox-mudem mudem-static

umlbox-mudem: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) -o umlbox-mudem

mudem-static: $(SOBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(STATICFLAGS) $(SOBJS) -o mudem-static
	-$(STRIP) mudem-static

.SUFFIXES: .c .o

%.o: %.c
	$(CC) $(CFLAGS) -c $<

%-static.o: %.c
	$(CC) $(CFLAGS) $(STATICFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(SOBJS) umlbox-mudem mudem-static
	rm -f deps

mrproper: clean
	-true

install:
	install -D umlbox-mudem $(DESTDIR)$(PREFIX)/bin/umlbox-mudem


include deps

deps: *.c *.h
	$(CC) -MM *.c > deps
