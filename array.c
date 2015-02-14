#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"
#include "array.h"

void
array_free(array_t *a)
{
	free(a->ptr);
	a->ptr = 0;
	a->amemb = 0;
	a->nmemb = 0;
}

bool
array_realloc(array_t *a, size_t size, size_t amemb)
{
	a->amemb = amemb;
	a->ptr = realloc(a->ptr, a->amemb * size);
	if(!a->ptr) {
		a->amemb = 0;
		a->nmemb = 0;
		return true;
	}
	return false;
}

bool
array_resize(array_t *a, size_t size, size_t nmemb)
{
	size_t next = next_size(nmemb, size * 8);
	a->nmemb = nmemb;
	if(a->nmemb > a->amemb || next * 4 < a->amemb) {
		return array_realloc(a, size, next);
	}
	return false;
}

bool
array_extend(array_t *a, size_t size, size_t nmore)
{
	if(!nmore) {
		return false;
	}

	return array_resize(a, size, a->nmemb + nmore);
}

bool
array_shrink(array_t *a, size_t size, size_t nless)
{
	if(!nless) {
		return false;
	}
	if(nless > a->nmemb) {
		return true;
	}

	return array_resize(a, size, a->nmemb - nless);
}

bool
array_fragment_bounds(array_t *a, ssize_t start, ssize_t end,
		size_t *out_start, size_t *out_end)
{
	ssize_t s;
	ssize_t e;

	if(start >= 0) {
		s = start;
	} else {
		size_t last = a->nmemb > 0 ? a->nmemb-1 : 0;
		s = last + start+1;
	}

	if(end >= 0) {
		e = end;
	} else {
		e = a->nmemb + end+1;
	}

	if(s < 0 || e < 0 || s > e || (size_t)e > a->nmemb) {
		return true;
	}

	*out_start = s;
	*out_end = e;

	return false;
}

void
array_fragment_apply(array_t *a, size_t size, size_t start, size_t end,
		array_memb_func_t func)
{
	char *ptr = a->ptr;
	ptr += start * size;
	for(size_t i = start; i < end; i++) {
		func(ptr);
		ptr += size;
	}
}

void
array_fragment_shift(array_t *a, size_t size, size_t start, size_t end,
		ssize_t shift)
{
	size_t nmemb = end - start;

	char *first = a->ptr;
	first += start * size;
	memshift(shift, first, nmemb, size);
}

bool
array_fragment_resize(array_t *a, size_t size, size_t start, size_t end,
		size_t nmemb)
{
	ssize_t shift = nmemb - (end - start);
	if(shift > 0) {
		if(array_extend(a, size, shift)) {
			return true;
		}
	}

	array_fragment_shift(a, size, start, a->nmemb, shift);

	if(shift < 0) {
		if(array_shrink(a, size, -shift)) {
			return true;
		}
	}
	return false;
}

/*
void *
slice_get(slice_t *slice, ssize_t idx)
{
	size_t start;
	size_t end;
	slice_validate(slice, &start, &end);
	if(start == end) {
		return NULL;
	}

	if(idx >= 0) {
		idx += start;
	} else {
		size_t last = end > 0 ? end-1 : 0;
		idx = last + idx+1;
	}

	DIEIF(idx < 0);
	DIEIF((size_t)idx < start);
	DIEIF((size_t)idx > end);
	DIEIF((size_t)idx >= slice->array->nmemb);

	return (char*)slice->array->ptr + idx * slice->array->size;
}
*/
