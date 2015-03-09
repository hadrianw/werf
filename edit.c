#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"
#include "array.h"

#include "edit.h"
#include "utf.h"

void
file_insert_line(file_t *f, size_t line, char *buf, size_t buf_len)
{
	ARR_FRAG_RESIZE(&f->content, line, line, 1);

	memset(f->content.data + line, 0, sizeof f->content.data[0]);
	ARR_RESIZE(&f->content.data[line], buf_len);

	memcpy(f->content.data[line].data, buf, buf_len);
}

void
file_free(file_t *f)
{
	ARR_FRAG_APPLY(&f->content, 0, f->content.nmemb, (array_memb_func_t)array_free);
	ARR_FREE(&f->content);
}

int
address_cmp(address_t *a1, address_t *a2)
{
	if(a1->line == a2->line && a1->offset == a2->offset) {
		return 0;
	}
	if( a1->line < a2->line || (a1->line == a2->line && a1->offset < a2->offset) ) {
		return -1;
	} else {
		return 1;
	}
}

int
range_from_addresses(range_t *rng, address_t *a1, address_t *a2)
{
	int cmp = address_cmp(a1, a2);
	if(cmp < 0) {
		rng->start = *a1;
		rng->end = *a2;
	} else {
		rng->start = *a2;
		rng->end = *a1;
	}
	return cmp;
}

void
range_fix_start(range_t *rng)
{
	if(address_cmp(&rng->start, &rng->end) > 0) {
		rng->start = rng->end;
	}
}

void
range_fix_end(range_t *rng)
{
	if(address_cmp(&rng->start, &rng->end) > 0) {
		rng->end = rng->start;
	}
}

static void
range_mod_line(range_t *rng, char *mod_line, size_t mod_len)
{
	string_t *line = rng->file->content.data + rng->start.line;

	size_t end_offset;
	size_t rest_len;
	bool insert_new_line;

	if(rng->start.line == rng->end.line) {
		end_offset = rng->end.offset;
	} else {
		end_offset = line->nmemb;
	}

	rest_len = line->nmemb - end_offset;
	insert_new_line = (rest_len > 0 || rng->start.line == rng->end.line) &&
			mod_len > 0 && mod_line[mod_len - 1] == '\n';

	if(insert_new_line) {
		char *rest = line->data + end_offset;
		file_insert_line(rng->file, rng->end.line+1, rest, rest_len);
		line = rng->file->content.data + rng->start.line;
		line->nmemb -= rest_len;
	}

	ARR_FRAG_RESIZE(line, rng->start.offset, end_offset, mod_len);
	memcpy(line->data + rng->start.offset, mod_line, mod_len);

	if(mod_len > 0 && mod_line[mod_len - 1] == '\n') {
		rng->start.line++;
		rng->start.offset = 0;
		range_fix_end(rng);
		return;
	}

	rng->start.offset += mod_len;

	bool join_end_line = rest_len == 0 && rng->start.line+1 < rng->file->content.nmemb;
	if(join_end_line) {
		if(rng->start.line == rng->end.line) {
			rng->end.line++;
			rng->end.offset = 0;
		}
		string_t *rest_line = rng->file->content.data + rng->end.line;
		char *rest = rest_line->data + rng->end.offset;
		rest_len = rest_line->nmemb - rng->end.offset;

		ARR_FRAG_RESIZE(line, rng->start.offset, line->nmemb, rest_len);
		memcpy(line->data + rng->start.offset, rest, rest_len);

		ARR_FRAG_APPLY(&rng->file->content, rng->start.line+1, rng->end.line+1,
				(array_memb_func_t)array_free);
		ARR_FRAG_RESIZE(&rng->file->content, rng->start.line+1, rng->end.line+1, 0);
	}

	rng->end.line = rng->start.line;
	rng->end.offset = rng->start.offset;
}

static void
range_mod(range_t *rng, char *mod, size_t mod_len)
{
	size_t rest = mod_len;
	char *next;

	while(rest > 0) {
		next = memchr(mod, '\n', rest);
		if(next != NULL) {
			next++;
			mod_len = next - mod;
		} else {
			mod_len = rest;
		}

		range_mod_line(rng, mod, mod_len);

		mod = next;
		rest -= mod_len;
	}
	range_mod_line(rng, "", 0);
}

int
range_read(range_t *rng, int fd)
{
	char buf[BUFSIZ];
	ssize_t buf_len;
	while( (buf_len = read(fd, buf, sizeof buf)) ) {
		if(buf_len < 0) {
			return -1;
		}
		range_mod(rng, buf, buf_len);
	}

	return 0;
}

size_t
range_copy(range_t *rng, char *buf, size_t bufsiz)
{
	if(!bufsiz) {
		return 0;
	}

	size_t off = rng->start.offset;
	size_t siz;
	size_t len = 0;

	rng->start.offset = rng->end.offset;

	do {
		string_t *line = &rng->file->content.data[rng->start.line];
		if(rng->start.line == rng->end.line) {
			siz = rng->end.offset - off;
		} else {
			siz = line->nmemb - off;
		}
		if(len + siz > bufsiz) {
			siz = bufsiz - len;
			rng->start.offset = off + siz;
		} else if(rng->start.line != rng->end.line) {
			rng->start.line++;
		}
		memcpy(buf + len, line->data + off, siz);
		len += siz;
		off = 0;
	} while(len < bufsiz && rng->start.line < rng->end.line);
	return len;
}


static void
opbuf_extend(opbuf_t *u, size_t ext)
{
	u->nsiz += ext;
	if(u->nsiz > u->asiz) {
		u->asiz = next_size(u->nsiz, 128);
		u->first = xrealloc(u->first, u->asiz, 1);
	}
}

static void
opbuf_next(opbuf_t *u, range_t *rng, optype_t type)
{
	op_t *last = (op_t*)((char*)u->first + u->last);
	if(u->first != NULL && type != OP_Replace && type == last->type &&
			(type != OP_BackSpace ?
				address_cmp(&last->dst.end, &rng->start) :
				address_cmp(&rng->end, &last->dst.start) ) == 0 ) {
		return;
	}

	size_t oldsiz = u->nsiz;
	opbuf_extend(u, sizeof u->first[0]);

	op_t *next = (op_t*)((char*)u->first + oldsiz);
	memset(next, 0, sizeof next[0]);

	next->prev = u->last;
	next->type = type;

	switch(type) {
	case OP_BackSpace:
		next->src.start = rng->end;
		next->src.end = rng->end;
		break;
	case OP_Delete:
		next->src.start = rng->start;
		next->src.end = rng->start;
		break;
	default:
		next->src.start = rng->start;
		next->src.end = rng->end;
		break;
	}
	next->dst.start = rng->start;
	next->buf_len = 0;
	u->last = (char*)next - (char*)u->first;
}

static void
range_push_mod(range_t *rng, char *mod, size_t mod_len, opbuf_t *u, optype_t type)
{
	opbuf_next(u, rng, type);

	op_t *last = (op_t*)((char*)u->first + u->last);
	size_t off = rng->start.offset;
	size_t siz;
	for(size_t i = rng->start.line; i <= rng->end.line; i++) {
		string_t *line = &rng->file->content.data[i];
		if(i == rng->end.line) {
			siz = rng->end.offset - off;
		} else {
			siz = line->nmemb - off;
		}
		
		opbuf_extend(u, siz);
		last = (op_t*)((char*)u->first + u->last);

		if(type != OP_BackSpace) {
			memcpy(last->buf + last->buf_len, line->data + off, siz);
		} else {
			memmove(last->buf + siz, last->buf, last->buf_len);
			memcpy(last->buf, line->data + off, siz);
		}
		last->buf_len += siz;

		off = 0;
	}

	range_mod(rng, mod, mod_len);

	if(type == OP_BackSpace || type == OP_Delete) {
		last->dst.start = rng->start;
	}
	last->dst.end = rng->start;

	rng->file->dirty = true;
}

void
undo(opbuf_t *u, opbuf_t *r, range_t *rng)
{
	if(u->nsiz == 0) {
		return;
	}
	op_t *last = (op_t*)((char*)u->first + u->last);
	range_t dst = {last->dst.start, last->dst.end, rng->file};
	range_push_mod(&dst, last->buf, last->buf_len, r, last->type);
	rng->start = last->src.start;
	rng->end = last->src.end;

	u->nsiz -= sizeof last[0] + last->buf_len;
	u->last = last->prev;
}

void
range_push(range_t *rng, char *mod, size_t mod_len, optype_t type)
{
	rng->file->redobuf.nsiz = 0;
	rng->file->redobuf.last = 0;
	range_push_mod(rng, mod, mod_len, &rng->file->undobuf, type);
}

void
file_undo(range_t *rng)
{
	undo(&rng->file->undobuf, &rng->file->redobuf, rng);
}

void
file_redo(range_t *rng)
{
	undo(&rng->file->redobuf, &rng->file->undobuf, rng);
}
