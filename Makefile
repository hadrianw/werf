#OPTIM = -g -O0 -Werror
OPTIM = -O2
CFLAGS = -std=c1x $(OPTIM) -Wall -Wextra -pedantic \
	-Wno-missing-field-initializers \
	-Wno-missing-braces \
	`freetype-config --cflags` \
	-D_POSIX_C_SOURCE=200809L
LDFLAGS = -lrt -lcairo -lX11 `freetype-config --libs` -lfontconfig

CC = gcc

SRC = \
	util.c \
	array.c \
	pipe.c \
	utf.c \
	edit.c \
	font.c \
	view.c \
	draw.c \
	window.c \
	werf.c
OBJ = $(SRC:.c=.o)

all: werf

.c.o:
	@echo CC $<
	@$(CC) -c $(CFLAGS) $<

$(OBJ): util.h Makefile
window.o: window.h draw.h view.h
draw.o: draw.h view.h
view.o: view.h
utf.o: utf.h
font.o: font.h utf.h
edit.o: edit.h utf.h array.h
pipe.c: pipe.h array.h
werf.o: pipe.h edit.h font.h array.h

werf: $(OBJ)
	@echo CC -o $@
	@$(CC) -o $@ $(OBJ) $(LDFLAGS)

clean:
	rm -f werf $(OBJ)

.PHONY: all clean
