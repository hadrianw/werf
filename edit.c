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
	slice_resize(&(slice_t){&f->array, line, line}, 1);

	memset(f->lines + line, 0, sizeof f->lines[0]);
	f->lines[line].array.size = 1;
	array_resize(&f->lines[line].array, buf_len);

	memcpy(f->lines[line].buf, buf, buf_len);
}

void
dt_file_free(dt_file_t *f)
{
	slice_apply(&(slice_t){&f->array, 0, -1}, (slice_memb_func_t)array_free);
	free(f->lines);
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
	string_t *line = rng->file->lines + rng->start.line;

	size_t end_offset;
	size_t rest_len;
	bool insert_new_line;

	if(rng->start.line == rng->end.line) {
		end_offset = rng->end.offset;
	} else {
		end_offset = line->array.nmemb;
	}

	rest_len = line->array.nmemb - end_offset;
	insert_new_line = (rest_len > 0 || rng->start.line == rng->end.line) &&
			mod_len > 0 && mod_line[mod_len - 1] == '\n';

	if(insert_new_line) {
		char *rest = line->buf + end_offset;
		dt_file_insert_line(rng->file, rng->end.line+1, rest, rest_len);
		line = rng->file->lines + rng->start.line;
		line->array.nmemb -= rest_len;
	}

	slice_resize(&(slice_t){&line->array, rng->start.offset, end_offset}, mod_len);
	memcpy(line->buf + rng->start.offset, mod_line, mod_len);

	if(mod_len > 0 && mod_line[mod_len - 1] == '\n') {
		rng->start.line++;
		rng->start.offset = 0;
		dt_range_fix_end(rng);
		return;
	}

	rng->start.offset += mod_len;

	bool join_end_line = rest_len == 0 && rng->start.line+1 < rng->file->array.nmemb;
	if(join_end_line) {
		if(rng->start.line == rng->end.line) {
			rng->end.line++;
			rng->end.offset = 0;
		}
		string_t *rest_line = rng->file->lines + rng->end.line;
		char *rest = rest_line->buf + rng->end.offset;
		rest_len = rest_line->array.nmemb - rng->end.offset;

		slice_resize(&(slice_t){&line->array, rng->start.offset, -1}, rest_len);
		memcpy(line->buf + rng->start.offset, rest, rest_len);

		slice_t slice = {&rng->file->array, rng->start.line+1, rng->end.line+1};
		slice_apply(&slice, (slice_memb_func_t)array_free);
		slice_resize(&slice, 0);
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
		string_t *line = &rng->file->lines[i];
		if(i == rng->end.line) {
			siz = rng->end.offset - off;
		} else {
			siz = line->array.nmemb - off;
		}
		
		undobuf_extend(u, siz);
		last = (undo_t*)((char*)u->first + u->last);

		if(type != OP_BackSpace) {
			memcpy(last->buf + last->buf_len, line->buf + off, siz);
		} else {
			memmove(last->buf + siz, last->buf, last->buf_len);
			memcpy(last->buf, line->buf + off, siz);
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
