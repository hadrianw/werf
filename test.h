#include <stdio.h>

#define TEST_STRCMP_OP(r, op, x, ...) \
	do { if(! (strcmp(((r), (x)) op 0)) { \
		printf(__FILE__ ":%d ", __LINE__); \
		printf(__VA_ARGS__); \
		printf("\n\t" #r " " #op " " #x "\n" \
			"\treceived: %s\n" \
			"\texpected: %s\n", \
			(r), (x));\
		return -1; \
	} } while(0)

#define TEST_MEMCMP_OP(r, op, x, n, ...) \
	do { if(! (memcmp((r), (x), (n)) op 0)) { \
		printf(__FILE__ ":%d ", __LINE__); \
		printf(__VA_ARGS__); \
		printf("\n\t" #r " " #op " " #x "\n" \
			"\treceived: %.*s\n" \
			"\texpected: %.*s\n", \
			(int)(n), (r), (int)(n), (x));\
		return -1; \
	} } while(0)

#define TEST_OP(f, r, op, x, ...) \
	do { if(! ((r) op (x)) ) { \
		printf(__FILE__ ":%d ", __LINE__); \
		printf(__VA_ARGS__); \
		printf("\n\t" #r " " #op " " #x "\n" \
			"\treceived: " f "\n" \
			"\texpected: " f "\n", \
			(r), (x));\
		return -1; \
	} } while(0)

static __attribute__ ((format (printf, 1, 2))) void
test_check_format(const char *format, ...)
{
	(void)format;
}

#define STRIP_PARENS(...) __VA_ARGS__

#define TEST_CALL(buf, size, format, func, args) \
	(test_check_format((format), STRIP_PARENS args), \
	snprintf((buf), (size), ("%s(" format ")"), #func, STRIP_PARENS args), \
	func args)

#define DEBUG_CALL(format, func, args) \
	(test_check_format((format), STRIP_PARENS args), \
	fprintf(stderr, ("%s(" format ")"), #func, STRIP_PARENS args), \
	func args)
