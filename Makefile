lexicon: dbm_prog lexicon.dict
	./dbm_prog -i lexicon.dict -o lexicon
	rm -f lexicon.dir
	mv -f lexicon.pag lexicon

additions: /var/log/unknown.words
	cat /var/log/unknown.words | sort | uniq | sed "s/.*/& &/g" >additions
	echo -n >/var/log/unknown.words

new: additions
	mv -f lexicon.dict lexicon.dict.old
	cat lexicon.dict.old additions | sort | uniq >lexicon.dict

install: lexicon
	chown root.root lexicon
	mv -f lexicon /usr/local/lib/ru_tts

dbm_prog: dbm_prog.c
	gcc -s -O2 -o $@ $< -lgdbm

clean:
	rm -f additions lexicon.dict.old lexicon
