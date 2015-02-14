OPTIM = -g -O0
#OPTIM = -O2
CFLAGS = -std=c1x $(OPTIM) -Wall -Wextra -pedantic -Werror \
	-Wno-missing-field-initializers \
	`freetype-config --cflags` \
	-D_POSIX_C_SOURCE=200809L
LDFLAGS = -flto -lrt -lcairo -lX11 `freetype-config --libs` -lfontconfig

CC = gcc
GDB = gdb -batch -ex run -ex bt
VALGRIND = valgrind --leak-check=full --suppressions=valgrind.supp

SRC = \
	drawtext.c \
	pipe.c \
	edit.c \
	font.c \
	utf.c \
	array.c \
	util.c
OBJ = $(SRC:.c=.o)

all: drawtext

.c.o:
	@echo CC $<
	@$(CC) -c $(CFLAGS) $<

$(OBJ): util.h Makefile
utf.o: utf.h
font.o: font.h utf.h
edit.o: edit.h utf.h array.h
pipe.c: pipe.h array.h
drawtext.o: pipe.h edit.h font.h array.h

drawtext: $(OBJ)
	@echo CC -o $@
	@$(CC) -o $@ $(OBJ) $(LDFLAGS)

clean:
	rm -f drawtext $(OBJ)

run: drawtext
	./$<

gdb: drawtext
	$(GDB) ./$<

valgrind: drawtext
	$(VALGRIND) ./$< arch/fc-drawtext.c

valgrind-gensupp: drawtext
	$(VALGRIND) --gen-suppressions=all ./$< arch/fc-drawtext.c

.PHONY: all clean run gdb valgrind valgrind-gensupp
