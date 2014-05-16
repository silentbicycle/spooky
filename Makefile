PROJECT = spooky
OPTIMIZE = -O2
WARN = -Wall -pedantic
#PROF=-pg
CFLAGS += -std=c99 -g ${WARN} ${OPTIMIZE} ${PROF}

all: ${PROJECT} test_spooky

${PROJECT}: spooky.a

spooky.a: spooky_encoder.o spooky_decoder.o

test_spooky: test_${PROJECT}.c spooky_encoder.o spooky_decoder.o

test_spooky.c: greatest.h

*.o: Makefile

spooky_encoder.o: spooky_encoder.h
spooky_decoder.o: spooky_decoder.h

test: test_spooky
	./test_spooky

tags:
	etags *.[ch]

clean:
	rm -f ${PROJECT} *.o *.core *.{lst,hex} test_spooky
