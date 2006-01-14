# Makefile for the lexical database compiling

# Database name
DBNAME = lexicon

# Linux
GROUP = root
#
# Using Berkeley DB:
DEFS = -DBERKELEYDB
LIBS = -ldb
DBF = ${DBNAME}
#
# Using GDBM:
#DEFS = 
#LIBS = -lgdbm_compat
#DBF = ${DBNAME}.dir ${DBNAME}.pag

# FreeBSD
#GROUP = wheel
#DEFS = -DFBSD_DATABASE
#LIBS = 
#DBF = ${DBNAME}.db

# Installation paths
prefix = /usr/local
bindir = ${prefix}/bin
libdir = ${prefix}/lib/ru_tts
lispdir = ${prefix}/share/emacs/site-lisp
docdir = ${prefix}/share/doc/rulex

all: new db

db: ${DBF}

${DBF}: lexholder lexicon.dict
	@rm -f ${DBNAME}
	./lexholder -f lexicon.dict -v ${DBNAME}

additions: /var/log/unknown.words
	cat /var/log/unknown.words | \
	sort | \
	uniq | \
	sed "s/.*/& &/g" >>additions.draft
	echo -n >/var/log/unknown.words
	test "$EDITOR" && \
	${EDITOR} additions.draft && \
	mv -f additions.draft additions

new: additions
	mv -f lexicon.dict lexicon.dict.old
	cat lexicon.dict.old additions | sort | uniq >lexicon.dict

install-db: db
	install -d ${libdir}
	install -g ${GROUP} -o root -m 0644 -p ${DBF} ${libdir}

install: lexholder install-db
	install -d ${bindir} ${lispdir} ${docdir}
	install -g ${GROUP} -o root -m 0755 -p lexholder ${bindir}/lexholder-ru
	install -g ${GROUP} -o root -m 0644 -p rulex.el ${lispdir}
	install -g ${GROUP} -o root -m 0644 -p README ${docdir}

lexholder: lexholder.o coder.o
	gcc -s -o $@ $^ ${LIBS}

clean:
	rm -f *.o *.old additions ${DBF} lexholder

%.o:%.c
	gcc -c -O2 ${DEFS} -o $@ $<

coder.o: coder.c coder.h
lexholder.o: lexholder.c coder.h
