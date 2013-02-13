CC=gcc
CFLAGS=-g -O3
LDFLAGS=
STATICFLAGS=-static
STRIP=strip
LINUX=linux-3.7
DESTDIR=
PREFIX=/usr

all: nokernel umlbox-linux

nokernel: umlbox-initrd.gz umlbox-mudem

umlbox-initrd.gz: init
	echo init | cpio -H newc -o | gzip -9c > umlbox-initrd.gz

init: init.c
	$(CC) $(CFLAGS) $(LDFLAGS) $(STATICFLAGS) init.c -o init
	$(STRIP) init

umlbox-mudem: mudem/umlbox-mudem
	-$(STRIP) mudem/umlbox-mudem
	ln -f mudem/umlbox-mudem umlbox-mudem || cp -f mudem/umlbox-mudem umlbox-mudem

mudem/umlbox-mudem: mudem/*.c mudem/*.h
	cd mudem && $(MAKE)

umlbox-linux: $(LINUX)/linux
	-$(STRIP) $(LINUX)/linux
	ln -f $(LINUX)/linux umlbox-linux || cp -f $(LINUX)/linux umlbox-linux

$(LINUX)/linux: $(LINUX)/.config
	cd $(LINUX) && $(MAKE) ARCH=um

$(LINUX)/.config: umlbox-config
	cd $(LINUX) ; $(MAKE) ARCH=um defconfig
	cat umlbox-config >> $(LINUX)/.config
	cd $(LINUX) ; yes '' | $(MAKE) ARCH=um oldconfig

clean:
	rm -f umlbox-linux init umlbox-initrd.gz umlbox-mudem
	cd mudem && $(MAKE) clean
	-cd $(LINUX) && $(MAKE) ARCH=um clean

mrproper: clean
	-cd $(LINUX) && $(MAKE) ARCH=um mrproper

install:
	install -D umlbox $(DESTDIR)$(PREFIX)/bin/umlbox
	install -D umlbox-mudem $(DESTDIR)$(PREFIX)/bin/umlbox-mudem
	install -D -m 0644 umlbox.1 $(DESTDIR)$(PREFIX)/share/man/man1/umlbox.1
	install -D -m 0644 umlbox-mudem.1 $(DESTDIR)$(PREFIX)/share/man/man1/umlbox-mudem.1
	install -D -m 0644 umlbox-initrd.gz $(DESTDIR)$(PREFIX)/lib/umlbox/umlbox-initrd.gz
	-install -D umlbox-linux $(DESTDIR)$(PREFIX)/bin/umlbox-linux
