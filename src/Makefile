# Makefile for the lexical database holding utility

# Installation paths
DESTDIR = 
prefix = /usr/local

bindir = ${prefix}/bin
libdir = ${prefix}/lib
includedir = ${prefix}/include

# Compiling options
CC = gcc
CFLAGS = -Wall -O2 -D_REENTRANT
LFLAGS = 
LIBS = -ldb

all: lexholder rulex

lexholder: lexholder.o librulexdb.la
	libtool --mode=link ${CC} ${LFLAGS} -o $@ $^

rulex: rulex.o librulexdb.la
	libtool --mode=link ${CC} ${LFLAGS} -o $@ $^

librulexdb.la: coder.lo lexdb.lo
	libtool --mode=link ${CC} ${LFLAGS} -o $@ $^ -rpath ${libdir} ${LIBS}

install: lexholder rulex librulexdb.la
	libtool --mode=install install -D -p -s librulexdb.la ${DESTDIR}${libdir}/librulexdb.la
	libtool --mode=install install -D -p -s lexholder ${DESTDIR}${bindir}/lexholder-ru
	libtool --mode=install install -D -p -s rulex ${DESTDIR}${bindir}/rulex
	install -D -m 0644 -p lexdb.h ${DESTDIR}${includedir}/rulexdb.h

clean:
	libtool --mode=clean rm -f *.lo *.la lexholder rulex
	rm -f  *.o

coder.lo: coder.c coder.h
lexdb.lo: lexdb.c lexdb.h coder.h
lexholder.o: lexholder.c lexdb.h
rulex.o: rulex.c lexdb.h

%.lo:%.c
	libtool --mode=compile ${CC} -c ${CFLAGS} $<

%.o:%.c
	${CC} -c ${CFLAGS} $<
