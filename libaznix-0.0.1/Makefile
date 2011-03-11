# aznix source distribution makefile

SHELL = /bin/sh

PACKAGE = libaznix
VERSION = 0.0.1

prefix = /usr/local
exec_prefix = ${prefix}

libdir = ${exec_prefix}/lib64
includedir = ${prefix}/include
pkgincludedir = $(includedir)/aznix

LIBTOOL = libtool
CC = gcc
CXX = g++
CXXFLAGS = -D__STDC_LIMIT_MACROS -fcheck-new
CPPFLAGS = -D_REENTRANT -D_XOPEN_SOURCE=600 -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -DX86_64 -D__STDC_FORMAT_MACROS
LDFLAGS =
INCLUDES = -I. -I$(includedir)
LDFLAGS =
LTCOMPILE = $(LIBTOOL) --mode=compile $(CC) $(INCLUDES) $(CPPFLAGS) $(CFLAGS)
CCLD = $(CC)
LINK = $(LIBTOOL) --mode=link $(CCLD) $(CFLAGS) $(LDFLAGS) -o $@
CFLAGS = -g -Wall -Werror -m64 -pipe -fmessage-length=0

lib_LTLIBRARIES = libaznix.la libsyscalls.la

vpath %.c src
vpath %.cpp src
vpath %.s src

libaznix_la_SOURCES = \
	az_allocid.cpp \
	az_memory.c \
	az_pgroup.c

libaznix_la_OBJECTS = \
	az_allocid.lo \
	az_memory.lo \
	az_pgroup.lo

libsyscalls_la_SOURCES = \
	syscalls.s

libsyscalls_la_OBJECTS = \
	syscalls.lo

libs: $(lib_LTLIBRARIES)

.SUFFIXES: .c .cpp .lo .o .s

.s.lo:
	$(LTCOMPILE) $(ASFLAGS) -c -o $@ $<

.c.lo:
	$(LTCOMPILE) -c -o $@ $<

.cpp.lo:
	$(LTCOMPILE) $(CXXFLAGS) -c -o $@ $<

aznix:
	ln -s include aznix

$(libaznix_la_OBJECTS): aznix $(libaznix_la_SOURCES)

libaznix.la: $(libaznix_la_OBJECTS)
	$(LINK) $+ -rpath $(DESTDIR)$(libdir) -release $(VERSION)

libsyscalls.la: $(libsyscalls_la_OBJECTS)
	$(LINK) $+ -rpath $(DESTDIR)$(libdir) -release $(VERSION)

install: $(lib_LTLIBRARIES)
	mkdir -p $(DESTDIR)$(pkgincludedir)
	install -t $(DESTDIR)$(pkgincludedir) include/*.h
	mkdir -p $(DESTDIR)$(libdir)
	for lib in $(lib_LTLIBRARIES); do \
$(LIBTOOL) --mode=install install $$lib $(DESTDIR)$(libdir)/$$lib; \
done

DISTFILES = Makefile include/*.h src test/*.c

dist:
	rm -f $(PACKAGE)-$(VERSION)
	ln -s . $(PACKAGE)-$(VERSION)
	tar -cvzf $(PACKAGE)-$(VERSION).tar.gz $(addprefix $(PACKAGE)-$(VERSION)/,$(DISTFILES))

clean:
	rm -f $(PACKAGE)-$(VERSION)
	rm -f *.la *.lo *.o
	rm -rf .libs
	rm -f aznix
