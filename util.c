#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "util.h"

void *
xcalloc(size_t nmemb, size_t size)
{
	void *ptr = calloc(nmemb, size);
	if(ptr == NULL) {
		die("calloc failed.\n");
	}
	return ptr;
}

void *
xmalloc(size_t nmemb, size_t size)
{
	void *ptr = malloc(nmemb * size);
	if(ptr == NULL) {
		die("malloc failed.\n");
	}
	return ptr;
}

void *
xrealloc(void *ptr, size_t nmemb, size_t size)
{
	ptr = realloc(ptr, nmemb * size);
	if(nmemb > 0 && ptr == NULL) {
		die("realloc failed.\n");
	}
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

void *
memshift(ssize_t shift, void *buf, size_t nmemb, size_t size)
{
	size_t shiftabs = ABS(shift);
	if(shift == 0 || shiftabs >= nmemb) {
		return buf;
	}
	size_t tomove = nmemb - shiftabs;
	char *src = buf;
	char *dst = buf;

	if(shift > 0) {
		dst += shift * size;
	} else {
		src -= shift * size;
	}

	return memmove(dst, src, tomove * size);
}

void
die(const char *fmt, ...) {
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	exit(-1);
}
