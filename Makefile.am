## Process this file with automake to produce Makefile.in

ACLOCAL_AMFLAGS = -I m4

SUBDIRS = src data manpages
doc_DATA = README README.ru
EXTRA_DIST = LICENSE.GPL-2 LICENSE.LGPL-2 README.ru rulex.el workshop
MAINTAINERCLEANFILES = @srcdir@/Makefile.in @top_srcdir@/aclocal.m4 \
	@top_srcdir@/compile @top_srcdir@/config.guess \
	@top_srcdir@/config.sub @top_srcdir@/configure \
	@top_srcdir@/depcomp @top_srcdir@/install-sh \
	@top_srcdir@/missing @top_srcdir@/ltmain.sh \
	@top_srcdir@/m4/libtool.m4 $(wildcard @top_srcdir@/m4/lt*.m4)

test: lexholder db
	$(MAKE) -e -C data $@

lexholder:
	$(MAKE) -e -C src lexholder

db: lexholder 
	$(MAKE) -e -C data all

install-db: db
	$(MAKE) -e -C data install
