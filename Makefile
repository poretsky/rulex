# Linux
DEFS = -DBERKELEYDB4
LIBS = -ldb
OWNER = root.root

# FreeBSD
#DEFS = 
#OWNER = root:wheel
#DBFILE = lexicon.db

lexicon: dbm_prog lexicon.dict
	./dbm_prog -i lexicon.dict -o lexicon

additions: /var/log/unknown.words
	cat /var/log/unknown.words | sort | uniq | sed "s/.*/& &/g" >additions
	echo -n >/var/log/unknown.words

new: additions
	mv -f lexicon.dict lexicon.dict.old
	cat lexicon.dict.old additions | sort | uniq >lexicon.dict

install: lexicon
	chown ${OWNER} lexicon
	mv -f lexicon /usr/local/lib/ru_tts

dbm_prog: dbm_prog.c
	gcc -s -O2 ${DEFS} -o $@ $< ${LIBS}

clean:
	rm -f additions lexicon.dict.old lexicon
