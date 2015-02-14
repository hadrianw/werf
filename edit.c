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
dt_file_insert_line(dt_file_t *f, size_t line, char *buf, size_t buf_len)
{
	ARR_FRAG_RESIZE(f, line, line, 1);

	memset(f->data + line, 0, sizeof f->data[0]);
	ARR_RESIZE(&f->data[line], buf_len);

	memcpy(f->data[line].data, buf, buf_len);
}

void
dt_file_free(dt_file_t *f)
{
	ARR_FRAG_APPLY(f, 0, f->nmemb, (array_memb_func_t)array_free);
	ARR_FREE(f);
}

int
dt_address_cmp(dt_address_t *a1, dt_address_t *a2)
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
dt_range_from_addresses(dt_range_t *rng, dt_address_t *a1, dt_address_t *a2)
{
	int cmp = dt_address_cmp(a1, a2);
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
dt_range_fix_start(dt_range_t *rng)
{
	if(dt_address_cmp(&rng->start, &rng->end) > 0) {
		rng->start = rng->end;
	}
}

void
dt_range_fix_end(dt_range_t *rng)
{
	if(dt_address_cmp(&rng->start, &rng->end) > 0) {
		rng->end = rng->start;
	}
}

void
dt_range_mod_line(dt_range_t *rng, char *mod_line, size_t mod_len)
{
	string_t *line = rng->file->data + rng->start.line;

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
		dt_file_insert_line(rng->file, rng->end.line+1, rest, rest_len);
		line = rng->file->data + rng->start.line;
		line->nmemb -= rest_len;
	}

	ARR_FRAG_RESIZE(line, rng->start.offset, end_offset, mod_len);
	memcpy(line->data + rng->start.offset, mod_line, mod_len);

	if(mod_len > 0 && mod_line[mod_len - 1] == '\n') {
		rng->start.line++;
		rng->start.offset = 0;
		dt_range_fix_end(rng);
		return;
	}

	rng->start.offset += mod_len;

	bool join_end_line = rest_len == 0 && rng->start.line+1 < rng->file->nmemb;
	if(join_end_line) {
		if(rng->start.line == rng->end.line) {
			rng->end.line++;
			rng->end.offset = 0;
		}
		string_t *rest_line = rng->file->data + rng->end.line;
		char *rest = rest_line->data + rng->end.offset;
		rest_len = rest_line->nmemb - rng->end.offset;

		ARR_FRAG_RESIZE(line, rng->start.offset, line->nmemb, rest_len);
		memcpy(line->data + rng->start.offset, rest, rest_len);

		ARR_FRAG_APPLY(rng->file, rng->start.line+1, rng->end.line+1, (array_memb_func_t)array_free);
		ARR_FRAG_RESIZE(rng->file, rng->start.line+1, rng->end.line+1, 0);
	}

	rng->end.line = rng->start.line;
	rng->end.offset = rng->start.offset;
}

void
dt_range_mod(dt_range_t *rng, char *mod, size_t mod_len)
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

		dt_range_mod_line(rng, mod, mod_len);

		mod = next;
		rest -= mod_len;
	}
	dt_range_mod_line(rng, "", 0);
}

int
dt_range_read(dt_range_t *rng, int fd)
{
	char buf[BUFSIZ];
	ssize_t buf_len;
	while( (buf_len = read(fd, buf, sizeof buf)) ) {
		if(buf_len < 0) {
			return -1;
		}
		dt_range_mod(rng, buf, buf_len);
	}

	return 0;
}

size_t
dt_range_copy(dt_range_t *rng, char *buf, size_t bufsiz)
{
	if(!bufsiz) {
		return 0;
	}

	size_t off = rng->start.offset;
	size_t siz;
	size_t len = 0;

	rng->start.offset = rng->end.offset;

	do {
		string_t *line = &rng->file->data[rng->start.line];
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
undobuf_extend(undobuf_t *u, size_t ext)
{
	u->nsiz += ext;
	if(u->nsiz > u->asiz) {
		u->asiz = next_size(u->nsiz, 128);
		u->first = xrealloc(u->first, u->asiz, 1);
	}
}

void
undobuf_next(undobuf_t *u, dt_range_t *rng, dt_optype_t type)
{
	undo_t *last = (undo_t*)((char*)u->first + u->last);
	if(u->first != NULL && type != OP_Replace && type == u->last_type &&
			(type != OP_BackSpace ?
				dt_address_cmp(&last->dst.end, &rng->start) :
				dt_address_cmp(&rng->end, &last->dst.start) ) == 0 ) {
		return;
	}

	u->last_type = type;

	size_t oldsiz = u->nsiz;
	undobuf_extend(u, sizeof u->first[0]);

	undo_t *next = (undo_t*)((char*)u->first + oldsiz);
	memset(next, 0, sizeof next[0]);

	next->prev = u->last;
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

void
dt_range_push_mod(dt_range_t *rng, char *mod, size_t mod_len, undobuf_t *u, dt_optype_t type)
{
	undobuf_next(u, rng, type);

	undo_t *last = (undo_t*)((char*)u->first + u->last);
	size_t off = rng->start.offset;
	size_t siz;
	for(size_t i = rng->start.line; i <= rng->end.line; i++) {
		string_t *line = &rng->file->data[i];
		if(i == rng->end.line) {
			siz = rng->end.offset - off;
		} else {
			siz = line->nmemb - off;
		}
		
		undobuf_extend(u, siz);
		last = (undo_t*)((char*)u->first + u->last);

		if(type != OP_BackSpace) {
			memcpy(last->buf + last->buf_len, line->data + off, siz);
		} else {
			memmove(last->buf + siz, last->buf, last->buf_len);
			memcpy(last->buf, line->data + off, siz);
		}
		last->buf_len += siz;

		off = 0;
	}

	dt_range_mod(rng, mod, mod_len);

	if(type == OP_BackSpace || type == OP_Delete) {
		last->dst.start = rng->start;
	}
	last->dst.end = rng->start;
}

void
dt_undo(undobuf_t *u, undobuf_t *r, dt_range_t *rng)
{
	if(u->nsiz == 0) {
		return;
	}
	undo_t *last = (undo_t*)((char*)u->first + u->last);
	dt_range_t dst = {last->dst.start, last->dst.end, rng->file};
	dt_range_push_mod(&dst, last->buf, last->buf_len, r, OP_Replace);
	rng->start = last->src.start;
	rng->end = last->src.end;

	u->nsiz -= sizeof last[0] + last->buf_len;
	u->last = last->prev;
	u->last_type = OP_None;
}
