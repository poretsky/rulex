# Makefile for the lexical database compiling

# Linux
DEFS = -DBERKELEYDB
LIBS = -ldb-3
#LIBS = -lgdbm_compat
OWNER = root.root

# FreeBSD
#DEFS = -DFBSD_DATABASE
#LIBS = 
#OWNER = root:wheel

all: new lexicon

lexicon: lexholder lexicon.dict
	@rm -f lexicon
	./lexholder -f lexicon.dict -v lexicon

additions: /var/log/unknown.words
	cat /var/log/unknown.words | \
	sort | \
	uniq | \
	sed "s/.*/& &/g" >>additions.raw
	echo -n >/var/log/unknown.words
	test "$EDITOR" && \
	${EDITOR} additions.raw && \
	mv -f additions.raw additions

new: additions
	mv -f lexicon.dict lexicon.dict.old
	cat lexicon.dict.old additions | sort | uniq >lexicon.dict

install: lexicon
	chown ${OWNER} lexicon
	mv -f lexicon /usr/local/lib/ru_tts

lexholder: lexholder.o coder.o
	gcc -s -o $@ $^ ${LIBS}

clean:
	rm -f *.o *.old *.dir *.pag *.db additions lexicon

%.o:%.c
	gcc -c -O2 ${DEFS} -o $@ $<

coder.o: coder.c coder.h
lexholder.o: lexholder.c coder.h
