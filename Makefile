CC := gcc
DEBUG_FLAGS    := -Wall -pedantic -g -fsanitize=leak -fsanitize=undefined -fsanitize=address -msse4.2
RELEASE_FLAGS  := -march=native -mtune=native -O3 -flto -msse4.2

a.out := main.o a.o
b.out := main.o b.o
c.out := main.o c.o
d.out := main.o d.o

SRC := $(wildcard *.c)
OBJ := ${SRC:c=o}
TARGETS := a.out b.out c.out d.out

.PHONY: clean all release debug bench

all: debug

bench: release
	hyperfine -N './a.out lorem 22' './b.out lorem 22' './c.out lorem 22' './d.out lorem 22'

clean:
	rm *.out *.o

debug: CFLAGS := ${DEBUG_FLAGS}
debug: ${TARGETS}

release: CFLAGS := ${RELEASE_FLAGS}
release: ${TARGETS}

${TARGETS}: ${OBJ}
	${CC} ${CFLAGS} ${$@} -o $@
	
%.o: %.c
	${CC} ${CFLAGS} $< -c -o $@
