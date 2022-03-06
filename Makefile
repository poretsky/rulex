# Makefile for the lexical database compiling

# Export all variables
export

# Installation paths
DESTDIR = 
prefix = /usr/local
docdir = ${prefix}/share/doc/rulex

all: lexholder db

.PHONY: lexholder
lexholder:
	$(MAKE) -e -C src all

.PHONY: db
db: lexholder 
	$(MAKE) -e -C data all

.PHONY: install-lexholder
install-lexholder: lexholder
	$(MAKE) -e -C src install
	install -d ${DESTDIR}${docdir}
	install -m 0644 -p README README.ru ${DESTDIR}${docdir}

.PHONY: install
install: install-lexholder db
	$(MAKE) -e -C data install

.PHONY: clean
clean:
	$(MAKE) -C src clean
	$(MAKE) -C data clean
