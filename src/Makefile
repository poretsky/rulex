# Makefile for the lexical database holding utility

# Installation paths
DESTDIR = 
prefix = /usr/local

bindir = ${prefix}/bin
libdir = ${prefix}/lib
includedir = ${prefix}/include

# Compiling options
CC = gcc
CFLAGS ?= -Wall -O2
LDFLAGS ?=
LIBS = -ldb
solib_LFLAGS = -Wl,--version-script=rulex.vscript

all: lexholder rulex

lexholder: lexholder.o librulexdb.la
	libtool --mode=link ${CC} ${LDFLAGS} -o $@ $^

rulex: rulex.o librulexdb.la
	libtool --mode=link ${CC} ${LDFLAGS} -o $@ $^

librulexdb.la: coder.lo lexdb.lo
	libtool --mode=link ${CC} ${LDFLAGS} ${solib_LFLAGS} -o $@ $^ -rpath ${libdir} ${LIBS} -version-info 1:0:1

install: lexholder rulex librulexdb.la
	libtool --mode=install install -D -p librulexdb.la ${DESTDIR}${libdir}/librulexdb.la
	libtool --mode=install install -D -p lexholder ${DESTDIR}${bindir}/lexholder-ru
	libtool --mode=install install -D -p rulex ${DESTDIR}${bindir}/rulex
	install -D -m 0644 -p lexdb.h ${DESTDIR}${includedir}/rulexdb.h

clean:
	libtool --mode=clean rm -f *.lo *.la lexholder rulex
	rm -f  *.o

coder.lo: coder.c coder.h
lexdb.lo: lexdb.c lexdb.h coder.h
lexholder.o: lexholder.c lexdb.h
rulex.o: rulex.c lexdb.h

%.lo:%.c
	libtool --mode=compile ${CC} -c ${CFLAGS} ${CPPFLAGS} $<

%.o:%.c
	${CC} -c ${CFLAGS} ${CPPFLAGS} $<
