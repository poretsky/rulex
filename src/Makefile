# Makefile for the lexical database holding utility

all: lexholder

lexholder: lexholder.o coder.o lexdb.o
	${CC} ${LFLAGS} -o $@ $^ ${LIBS}

clean:
	rm -f *.o lexholder

coder.o: coder.c coder.h
lexdb.o: lexdb.c lexdb.h coder.h
lexholder.o: lexholder.c lexdb.h

%.o:%.c
	gcc -c ${CFLAGS} -o $@ $<
