## Process this file with automake to produce Makefile.in

# Force KOI8-R coding system
LANG = ru_RU.KOI8-R

# Installation
dictdir = @datarootdir@/freespeech
dict_DATA = lexicon

# Source data
LEXDATA = explicit.dict implicit.dict \
	general.rules lexicon.rules prefix.rules correction.rules

# Distribution
EXTRA_DIST = $(LEXDATA) fixes.dict lexicon.test

# Cleaning
CLEANFILES = lexicon lexicon.dump
MAINTAINERCLEANFILES = @srcdir@/Makefile.in

lexicon: $(LEXDATA:%=@srcdir@/%)
	@rm -f lexicon
	@echo "Inserting explicit dictionary:"
	../src/lexholder -f @srcdir@/explicit.dict -X lexicon
	@echo "Inserting implicit dictionary:"
	../src/lexholder -f @srcdir@/implicit.dict -M lexicon
	@echo "Inserting lexical classifiers:"
	../src/lexholder -f @srcdir@/lexicon.rules -L lexicon
	@echo "Inserting general rules:"
	../src/lexholder -f @srcdir@/general.rules -G lexicon
	@echo "Inserting prefix rules:"
	../src/lexholder -f @srcdir@/prefix.rules -P lexicon
	@echo "Inserting correction rules:"
	../src/lexholder -f @srcdir@/correction.rules -C lexicon

test: lexicon @srcdir@/lexicon.test
	../src/lexholder -v -f lexicon.dump -t @srcdir@/lexicon.test lexicon

dbcleanup: lexicon
	../src/lexholder -c lexicon

extensive-dbcleanup: dbcleanup @srcdir@/fixes.dict
	../src/lexholder -cM lexicon
	../src/lexholder -rf @srcdir@/fixes.dict -M lexicon

extract:
	@echo "Extracting general rules:"
	@rm -f general.rules
	../src/lexholder -f general.rules -G -l lexicon
	@echo "Extracting lexical classification rules:"
	@rm -f lexicon.rules
	../src/lexholder -f lexicon.rules -L -l lexicon
	@echo "Extracting prefix rules:"
	@rm -f prefix.rules
	../src/lexholder -f prefix.rules -P -l lexicon
	@echo "Extracting correction rules:"
	@rm -f correction.rules
	../src/lexholder -f correction.rules -C -l lexicon
	@echo "Extracting implicit dictionary:"
	@rm -f implicit.dict
	../src/lexholder -M -l lexicon | sort | uniq >implicit.dict
	@echo "Extracting explicit dictionary:"
	@rm -f explicit.dict
	../src/lexholder -X -l lexicon | sort | uniq >explicit.dict

install-data-hook:
	cd $(DESTDIR)$(dictdir) && mv -f lexicon rulex.db
