## Process this file with automake to produce Makefile.in

bin_PROGRAMS = rulex lexholder
lib_LTLIBRARIES = librulexdb.la
include_HEADERS = lexdb.h

AM_CFLAGS = -g -Wall -Wno-unused-result -O2

rulex_SOURCES = rulex.c
rulex_LDADD = librulexdb.la

lexholder_SOURCES = lexholder.c
lexholder_LDADD = librulexdb.la

librulexdb_la_CPPFLAGS = $(DB_CPPFLAGS)
librulexdb_la_LDFLAGS = $(DB_LDFLAGS) -version-info 1:0:1
librulexdb_la_LIBADD = $(DB_LIBS)

if HAVE_VSCRIPT
librulexdb_la_LDFLAGS += $(VSCRIPT_LDFLAGS),@srcdir@/rulex.vscript
endif

librulexdb_la_SOURCES = coder.c lexdb.c

EXTRA_DIST = coder.h rulex.vscript
MAINTAINERCLEANFILES = @srcdir@/Makefile.in

install-exec-hook:
	cd $(DESTDIR)$(bindir) && mv -f lexholder lexholder-ru
