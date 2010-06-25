# Makefile for css-tools
CC ?= gcc
CFLAGS ?= -Wall
VERSION ?= `git describe --tags`

all: cssi

cssi: cssi.c tags.h
	git describe --tags
	$(CC) $(CFLAGS) -o cssi cssi.c -DVERSION=\"$(VERSION)\"

dist: all
	-mkdir css-tools
	for p in *.c *.h; do cp $$p css-tools/$$p; done;
	cp Makefile css-tools/
	cp readme css-tools/
	tar -cvvf css-tools_$(VERSION).tar css-tools/
	gzip css-tools_$(VERSION).tar
	rm -r css-tools
