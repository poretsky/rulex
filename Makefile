# Linux
DEFS = -DBERKELEYDB
LIBS = -ldb-3
OWNER = root.root

# FreeBSD
#DEFS = -DFBSD_DATABASE
#LIBS = 
#OWNER = root:wheel

all: new lexicon

lexicon: dbm_prog lexicon.dict
	@rm -f lexicon
	./dbm_prog -i lexicon.dict -o lexicon

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

dbm_prog: dbm_prog.c coder.c
	gcc -s -O2 ${DEFS} -o $@ $^ ${LIBS}

clean:
	rm -f additions lexicon.dict.old lexicon
