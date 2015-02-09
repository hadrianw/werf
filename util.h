#include <sys/types.h>
#include <stddef.h>
#include <stdbool.h>

#define ABS(a) ((a) < (0) ? -(a) : (a))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) < (b) ? (b) : (a))
#define LEN(a) (sizeof(a) / sizeof(a)[0])
#define DEFAULT(a, b) (a) = (a) ? (a) : (b)
#define BETWEEN(x, a, b) ((a) <= (x) && (x) <= (b))

#define DIEIF(x) dieif(__FILE__, __LINE__, __FUNCTION__, #x, (x))

typedef unsigned char uchar;

static inline size_t
next_pow2(size_t n)
{
	n--;
	for(size_t i = 1; i < 8 * sizeof n; i*=2) {
		n |= i;
	}
	n++;
	return n;
}

static inline size_t
next_size(size_t n, size_t min)
{
	n = next_pow2(n);
	return n < min ? min : n * 3 / 2;
}

static inline size_t
clamps(size_t v, size_t min, size_t max)
{
	if(v < min) {
		return min;
	} else if(v > max) {
		return max;
	}
	return v;
}

static inline ssize_t
clampss(ssize_t v, ssize_t min, ssize_t max)
{
	if(v < min) {
		return min;
	} else if(v > max) {
		return max;
	}
	return v;
}

static inline size_t
oneless(size_t v) {
	if(v > 0) {
		return v - 1;
	}
	return v;
}

void *xcalloc(size_t nmemb, size_t size);
void *xmalloc(size_t nmemb, size_t size);
void *xrealloc(void *ptr, size_t nmemb, size_t size);
void *reallocdup(void *heap, size_t nnew, const void *stack, size_t nstack, size_t size);
void *memshift(ssize_t shift, void *src, size_t nmemb, size_t size);

void die(const char *fmt, ...);
void dieif(const char *file, int line, const char *func, const char *msg, int die_req);
