CC := gcc
DEBUG_FLAGS    := -Wall -pedantic -g -fsanitize=leak -fsanitize=undefined -fsanitize=address -lpthread -DBUF_SIZE=8192 -msse4.2
RELEASE_FLAGS  := -march=native -mtune=native -O3 -flto -lpthread -DBUF_SIZE=8192 -msse4.2

a.out := main.o a.o
b.out := main.o b.o
c.out := main.o c.o
d.out := main.o d.o
e.out := main.o e.o
f.out := main.o f.o
g.out := main.o g.o
h.out := main.o h.o
i.out := main.o i.o

SRC := $(wildcard *.c)
OBJ := ${SRC:c=o}
TARGETS := a.out b.out c.out d.out e.out f.out g.out h.out i.out

.PHONY: clean all release debug bench

all: debug

bench: release
	hyperfine --warmup 10 -N './a.out lorem 22' './b.out lorem 22' './c.out lorem 22' \
		'./d.out lorem 22' './e.out lorem 22' './f.out lorem 22' './g.out lorem 22' \
		'./h.out lorem 22' './i.out lorem 22'

clean:
	rm -f *.out *.o *.log

debug: CFLAGS := ${DEBUG_FLAGS}
debug: ${TARGETS}

release: CFLAGS := ${RELEASE_FLAGS}
release: ${TARGETS}

${TARGETS}: ${OBJ}
	${CC} ${CFLAGS} ${$@} -o $@
	
%.o: %.c
	${CC} ${CFLAGS} $< -c -o $@
