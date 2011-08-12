CC=gcc
CFLAGS=-O3
LDFLAGS=
STATICFLAGS=-static
LINUX=linux-3.0.1
PREFIX=/usr

all: umlbox-linux

umlbox-linux: $(LINUX)/linux
	-strip $(LINUX)/linux
	ln -f $(LINUX)/linux umlbox-linux || cp -f $(LINUX)/linux umlbox-linux

$(LINUX)/linux: umlboxinit/init $(LINUX)/.config
	-strip umlboxinit/init
	cd $(LINUX) ; $(MAKE) ARCH=um

$(LINUX)/.config: umlbox-config
	cp -f umlbox-config $(LINUX)/.config

umlboxinit/init: init.c
	-mkdir -p umlboxinit
	$(CC) $(CFLAGS) $(LDFLAGS) $(STATICFLAGS) init.c -o umlboxinit/init

clean:
	rm -f umlbox-linux umlboxinit/init
	-rmdir umlboxinit/
	cd $(LINUX) ; $(MAKE) ARCH=um clean

mrproper: clean
	cd $(LINUX) ; $(MAKE) ARCH=um mrproper

install:
	install -D umlbox $(PREFIX)/bin/umlbox
	install -D umlbox-linux $(PREFIX)/bin/umlbox-linux
