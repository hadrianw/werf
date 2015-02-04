#include "util.h"
#include "utf.h"

/* taken from st (http://st.suckless.org/) */

static uchar utfbyte[UTF_SIZ + 1] = {0x80, 0x00, 0xC0, 0xE0, 0xF0};
static uchar utfmask[UTF_SIZ + 1] = {0xC0, 0x80, 0xE0, 0xF0, 0xF8};
static long utfmin[UTF_SIZ + 1] = {0x000000, 0x00, 0x080, 0x0800, 0x010000};
static long utfmax[UTF_SIZ + 1] = {0x10FFFF, 0x7F, 0x7FF, 0xFFFF, 0x10FFFF};

long
utf8decodebyte(char c, size_t *i)
{
	for(*i = 0; *i < LEN(utfmask); ++(*i))
		if(((uchar)c & utfmask[*i]) == utfbyte[*i])
			return (uchar)c & ~utfmask[*i];
	return 0;
}

size_t
utf8validate(long *u, size_t i)
{
	if(!BETWEEN(*u, utfmin[i], utfmax[i]) || BETWEEN(*u, 0xD800, 0xDFFF))
		*u = UTF_INVALID;
	for(i = 1; *u > utfmax[i]; )
		i++;
	return i;
}

size_t
utf8decode(const char *c, long *u, size_t clen)
{
	size_t i, j, len, type;
	long udecoded;

	*u = UTF_INVALID;
	if(!clen)
		return 0;
	udecoded = utf8decodebyte(c[0], &len);
	if(!BETWEEN(len, 1, UTF_SIZ))
		return 1;
	for(i = 1, j = 1; i < clen && j < len; ++i, ++j) {
		udecoded = (udecoded << 6) | utf8decodebyte(c[i], &type);
		if(type != 0)
			return j;
	}
	if(j < len)
		return 0;
	*u = udecoded;
	utf8validate(u, len);
	return len;
}

size_t
utf8chsiz(char *c, size_t clen)
{
	if(clen <= 0) {
		return 0;
	}
	long cp;
	size_t chsiz = utf8decode(c, &cp, MIN(UTF_SIZ, clen));
	return cp != UTF_INVALID ? chsiz : 1;
}

/*
size_t
utf8chsiz_backward(char *c, size_t clen)
{
	if(clen <= 0) {
		return 0;
	}
	size_t len = MIN(UTF_SIZ, clen);

	size_t chsiz;
	long cp;
	for(size_t i = 1; i <= len; i++) {
		c--;
		chsiz = utf8decode(c, &cp, i);
		if(cp != UTF_INVALID && chsiz == i) {
			return chsiz;
		}
	}
	return 1;
}
*/

char
utf8encodebyte(long u, size_t i)
{
	return utfbyte[i] | (u & ~utfmask[i]);
}

size_t
utf8encode(long u, char *c, size_t clen)
{
	size_t len, i;

	len = utf8validate(&u, 0);
	if(clen < len)
		return 0;
	for(i = len - 1; i != 0; --i) {
		c[i] = utf8encodebyte(u, 0);
		u >>= 6;
	}
	c[0] = utf8encodebyte(u, len);
	return len;
}
