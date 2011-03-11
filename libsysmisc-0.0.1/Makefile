# sysmisc source distribution makefile

SHELL = /bin/sh

PACKAGE = libsysmisc
VERSION = 0.0.1

AZNIX_API_VERSION=200

prefix = /usr/local
exec_prefix = ${prefix}

libdir = ${exec_prefix}/lib64
includedir = ${prefix}/include
pkgincludedir = $(includedir)/os

LIBTOOL = libtool
CC = gcc
CXX = g++
CXXFLAGS = -D__STDC_LIMIT_MACROS -fcheck-new -DAZNIX_API_VERSION=${AZNIX_API_VERSION}
CPPFLAGS = -D_REENTRANT -D_XOPEN_SOURCE=600 -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE \
		   -DX86_64 -D__STDC_FORMAT_MACROS -DAZNIX_API_VERSION=${AZNIX_API_VERSION}
LDFLAGS =
INCLUDES = -I. -I$(includedir)
LDFLAGS =
LTCOMPILE = $(LIBTOOL) --tag=CC --mode=compile $(CC) $(INCLUDES) $(CPPFLAGS) $(CFLAGS)
CCLD = $(CC)
LINK = $(LIBTOOL) --tag=CC --mode=link $(CCLD) $(CFLAGS) $(LDFLAGS) -o $@
CFLAGS = -g -Wall -Werror -m64 -pipe -fmessage-length=0

lib_LTLIBRARIES = libsysmisc.la

vpath %.c src

libsysmisc_la_SOURCES = \
	memory.c \
	pi.c \
	sched.c \
	shared_user_data.c \
	token.c

libsysmisc_la_OBJECTS = $(patsubst %.c,%.lo,$(libsysmisc_la_SOURCES))

libs: $(lib_LTLIBRARIES)

.SUFFIXES: .c .cpp .lo .o .s

.s.lo:
	$(LTCOMPILE) $(ASFLAGS) -c -o $@ $<

.c.lo:
	$(LTCOMPILE) -c -o $@ $<

.cpp.lo:
	$(LTCOMPILE) $(CXXFLAGS) -c -o $@ $<

os:
	ln -s include $@

$(libsysmisc_la_OBJECTS): os $(libsysmisc_la_SOURCES)

libsysmisc.la: $(libsysmisc_la_OBJECTS)
	$(LINK) $+ -rpath $(DESTDIR)$(libdir) -release $(VERSION)

install: $(lib_LTLIBRARIES)
	mkdir -p $(DESTDIR)$(pkgincludedir)
	install -t $(DESTDIR)$(pkgincludedir) include/*.h
	mkdir -p $(DESTDIR)$(libdir)
	for lib in $(lib_LTLIBRARIES); do \
$(LIBTOOL) --mode=install install $$lib $(DESTDIR)$(libdir)/$$lib; \
done

DISTFILES = Makefile include/*.h src

dist:
	rm -f $(PACKAGE)-$(VERSION)
	ln -s . $(PACKAGE)-$(VERSION)
	tar -cvzf $(PACKAGE)-$(VERSION).tar.gz $(addprefix $(PACKAGE)-$(VERSION)/,$(DISTFILES))

clean:
	rm -f $(PACKAGE)-$(VERSION)
	rm -f *.la *.lo *.o
	rm -rf .libs
	rm -f os
