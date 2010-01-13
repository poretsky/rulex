# Makefile for the lexical database compiling

# Database file name
DBF = lexicon.db

# Utility executable name
UTILITY = lexholder-ru

# Linux
GROUP = root

# FreeBSD
#GROUP = wheel

# Installation paths
prefix = /usr/local
bindir = ${prefix}/bin
datadir = ${prefix}/share/ru_tts
lispdir = ${prefix}/share/emacs/site-lisp
docdir = ${prefix}/share/doc/rulex

# Compiling options
export CC = gcc
export CFLAGS = -O2
export LFLAGS = -s
export LIBS = -ldb

all: lexholder db

.PHONY: lexholder
lexholder:
	$(MAKE) -C src all

.PHONY: db
db: lexholder 
	$(MAKE) -C data all

install: lexholder install-db
	install -d ${bindir} ${lispdir} ${docdir}
	install -g ${GROUP} -o root -m 0755 -p src/lexholder ${bindir}/${UTILITY}
	install -g ${GROUP} -o root -m 0644 -p rulex.el ${lispdir}
	install -g ${GROUP} -o root -m 0644 -p README ${docdir}

install-db: db
	install -d ${datadir}
	install -g ${GROUP} -o root -m 0644 -p data/lexicon ${datadir}/${DBF}

.PHONY: clean
clean:
	$(MAKE) -C src clean
	$(MAKE) -C data clean

additions: unknown.words
	cat $^ | sort | uniq | sed "s/.*/& &/g" >> $@
