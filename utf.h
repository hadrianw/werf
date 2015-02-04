/* taken from st (http://st.suckless.org/) */

#define UTF_INVALID 0xFFFD
#define UTF_SIZ 4

long utf8decodebyte(char c, size_t *i);
size_t utf8validate(long *u, size_t i);
size_t utf8decode(const char *c, long *u, size_t clen);
size_t utf8chsiz(char *c, size_t clen);
//size_t utf8chsiz_backward(char *c, size_t clen);
char utf8encodebyte(long u, size_t i);
size_t utf8encode(long u, char *c, size_t clen);
