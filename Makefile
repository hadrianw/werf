OPTIM = -g -O0 -Werror
#OPTIM = -O2
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
	command.c \
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
	@$(CC) -c $(CFLAGS) $< -o $@

$(OBJ): util.h Makefile
window.o: window.h draw.h view.h
draw.o: draw.h view.h
view.o: view.h
utf.o: utf.h
font.o: font.h utf.h
edit.o: edit.h utf.h array.h
pipe.o: pipe.h array.h
command.o: command.h array.h view.h edit.h
werf.o: pipe.h edit.h font.h array.h

tests.h: $(SRC) gen-tests.h.awk
	@echo GEN tests.h
	@./gen-tests.h.awk $(SRC) > tests.h
tests.o: tests.c tests.h
tests.passed: tests.o $(OBJ)
	@echo CC -o tests
	@$(CC) -o tests tests.o -Wl,--wrap=main $(OBJ) $(LDFLAGS)
	@echo testing
	valgrind --quiet --leak-check=full --error-exitcode=1 ./tests
	mv tests tests.passed

werf: $(OBJ) tests.passed
	@echo CC -o $@
	@$(CC) -o $@ $(OBJ) $(LDFLAGS)

clean:
	rm -f werf tests tests.passed tests.h $(OBJ)

.PHONY: all clean
