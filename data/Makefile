# Maintaining lexical database

# Force KOI8-R coding system
LANG = ru_RU.KOI8-R

all: lexicon

lexicon: explicit.dict implicit.dict \
 general.rules lexicon.rules correction.rules
	@rm -f lexicon
	@echo "Inserting explicit dictionary:"
	../src/lexholder -f explicit.dict -X lexicon
	@echo "Inserting implicit dictionary:"
	../src/lexholder -f implicit.dict -M lexicon
	@echo "Inserting lexical classifiers:"
	../src/lexholder -f lexicon.rules -L lexicon
	@echo "Inserting general rules:"
	../src/lexholder -f general.rules -G lexicon
	@echo "Inserting correction rules:"
	../src/lexholder -f correction.rules -C lexicon

test: lexicon lexicon.test
	../src/lexholder -v -f lexicon.dump -t lexicon.test lexicon

dbcleanup: lexicon
	../src/lexholder -c lexicon

clean:
	rm -f  lexicon

extract:
	@echo "Extracting general rules:"
	@rm -f general.rules
	../src/lexholder -f general.rules -G -l lexicon
	@echo "Extracting lexical classification rules:"
	@rm -f lexicon.rules
	../src/lexholder -f lexicon.rules -L -l lexicon
	@echo "Extracting correction rules:"
	@rm -f correction.rules
	../src/lexholder -f correction.rules -C -l lexicon
	@echo "Extracting implicit dictionary:"
	@rm -f implicit.dict
	../src/lexholder -M -l lexicon | sort | uniq >implicit.dict
	@echo "Extracting explicit dictionary:"
	@rm -f explicit.dict
	../src/lexholder -X -l lexicon | sort | uniq >explicit.dict
