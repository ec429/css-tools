# Makefile for css-tools
CC ?= gcc
CFLAGS ?= -Wall
CCW ?= i586-mingw32msvc-gcc
CFLAGSW ?= -Wall
VERSION ?= `git describe --tags`
PREFIX ?= /usr/local

all: cssi csscover
	git describe --tags
	
install: $(PREFIX)/bin/cssi $(PREFIX)/bin/csscover

cssi: cssi.c tags.h
	$(CC) $(CFLAGS) -o cssi cssi.c -DVERSION=\"$(VERSION)\"

csscover: csscover.c tags.h
	$(CC) $(CFLAGS) -o csscover csscover.c -DVERSION=\"$(VERSION)\"

$(PREFIX)/bin/cssi: cssi
	install -sD cssi $(PREFIX)/bin/cssi

$(PREFIX)/bin/csscover: csscover
	install -sD csscover $(PREFIX)/bin/csscover

dist: all
	-mkdir css-tools
	for p in *.c *.h; do cp $$p css-tools/$$p; done;
	cp cssi css-tools/
	cp csscover css-tools/
	cp Makefile css-tools/
	cp readme css-tools/
	cp changelog css-tools/
	tar -cvvf css-tools_$(VERSION).tar css-tools/
	gzip css-tools_$(VERSION).tar
	rm -r css-tools

all-w: cssi.exe csscover.exe
git describe --tags

cssi.exe: cssi.c tags.h
	$(CCW) $(CFLAGSW) -o cssi.exe cssi.c -DVERSION=\"$(VERSION)\"

csscover.exe: csscover.c tags.h
	$(CCW) $(CFLAGSW) -o csscover.exe csscover.c -DVERSION=\"$(VERSION)\"

distw: all-w
	-mkdir css-tools
	for p in *.c *.h *.exe; do cp $$p css-tools/$$p; done;
	cp Makefile css-tools/
	cp readme css-tools/
	cp changelog css-tools/

