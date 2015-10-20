#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "util.h"

void *
xcalloc(size_t nmemb, size_t size)
{
	void *ptr = calloc(nmemb, size);
	DIEIF(ptr == NULL);
	return ptr;
}

void *
xmalloc(size_t nmemb, size_t size)
{
	void *ptr = malloc(nmemb * size);
	DIEIF(ptr == NULL);
	return ptr;
}

void *
xrealloc(void *ptr, size_t nmemb, size_t size)
{
	ptr = realloc(ptr, nmemb * size);
	DIEIF(nmemb > 0 && ptr == NULL);
	return ptr;
}

void *
reallocdup(void *heap, size_t nnew, const void *stack, size_t nstack, size_t size)
{
	if(heap) {
		heap = xrealloc(heap, nnew, size);
	} else {
		heap = xmalloc(nnew, size);
		if(stack) {
			memcpy(heap, stack, size * nstack);
		}
	}
	return heap;
}

TEST(memshift) {
	{
		char buf[] = "abc";
		memshift(-1, buf, sizeof(buf)-1, sizeof(buf[0]));
		assert(!strcmp(buf, "bcc"));
	} {
		char buf[] = "abc";
		memshift(1, buf, sizeof(buf)-1, sizeof(buf[0]));
		assert(!strcmp(buf, "aab"));
	}
}

void
memshift(ssize_t shift, void *buf, size_t nmemb, size_t size)
{
	size_t shiftabs = ABS(shift);
	if(shift == 0 || shiftabs >= nmemb) {
		return;
	}
	size_t tomove = nmemb - shiftabs;
	char *src = buf;
	char *dst = buf;

	if(shift > 0) {
		dst += shift * size;
	} else {
		src -= shift * size;
	}

	memmove(dst, src, tomove * size);
}

void
die(const char *fmt, ...) {
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	exit(-1);
}

void
dieif(const char *file, int line, const char *func, const char *msg, int die_req)
{
	if(die_req) {
		die("%s:%d:%s: fatal error: %s (%s)\n", file, line, func, msg, strerror(errno));
	}
}

