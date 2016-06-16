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
pipe.c: pipe.h array.h
werf.o: pipe.h edit.h font.h array.h

test/main.c: $(OBJ) Makefile
	@echo generate test objects
	@mkdir -p test
	@cp $(OBJ) test
	@objcopy -N main test/werf.o
	@sh gen-test.sh $(OBJ) > test/main.c
test/main.o: test/main.c
test-werf.passed: test/main.o test/werf.o
	@echo CC -o test-werf
	@cd test && $(CC) -o ../test-werf main.o $(OBJ) $(LDFLAGS)
	@echo testing
	valgrind --quiet --leak-check=full --error-exitcode=1 ./test-werf
	mv test-werf test-werf.passed

werf: $(OBJ) test-werf.passed
	@echo CC -o $@
	@$(CC) -o $@ $(OBJ) $(LDFLAGS)

clean:
	rm -f werf test-werf test-werf.passed $(OBJ)

.PHONY: all clean
