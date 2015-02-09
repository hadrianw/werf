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

void
array_resize(array_t *a, size_t n)
{
	size_t next = next_size(n, a->size * 8);
	a->nmemb = n;
	if(a->nmemb > a->amemb || next * 4 < a->amemb) {
		a->amemb = next;
		a->ptr = xrealloc(a->ptr, a->amemb, a->size);
	}
}

void
array_extend(array_t *a, ssize_t nchange)
{
	DIEIF(-nchange > (ssize_t)a->nmemb);
	array_resize(a, a->nmemb + nchange);
}

static void
slice_validate(slice_t *slice, size_t *out_start, size_t *out_end)
{
	DIEIF(slice == NULL);
	DIEIF(slice->array == NULL);
	
	ssize_t start;
	ssize_t end;

	if(slice->start >= 0) {
		start = slice->start;
	} else {
		size_t last = slice->array->nmemb > 0 ? slice->array->nmemb-1 : 0;
		start = last + slice->start+1;
	}

	if(slice->end >= 0) {
		end = slice->end;
	} else {
		end = slice->array->nmemb + slice->end+1;
	}

	DIEIF(start < 0);
	DIEIF(end < 0);
	DIEIF(start > end);
	DIEIF((size_t)end > slice->array->nmemb);

	*out_start = start;
	*out_end = end;
}

void
slice_apply(slice_t *slice, slice_memb_func_t func)
{
	size_t start;
	size_t end;
	slice_validate(slice, &start, &end);

	char *ptr = slice->array->ptr;
	ptr += start * slice->array->size;
	for(size_t i = start; i < end; i++) {
		func(ptr);
		ptr += slice->array->size;
	}
}

void
slice_shift(slice_t *slice, ssize_t shift)
{
	size_t start;
	size_t end;
	slice_validate(slice, &start, &end);

	size_t nmemb = end - start;

	char *first = slice->array->ptr;
	first += start * slice->array->size;
	memshift(shift, first, nmemb, slice->array->size);
}

void
slice_resize(slice_t *slice, size_t nmemb)
{
	size_t start;
	size_t end;
	slice_validate(slice, &start, &end);

	ssize_t shift = nmemb - (end - start);
	if(shift > 0) {
		array_extend(slice->array, shift); 
	}

	slice_shift(&(slice_t){slice->array, start, -1}, shift);

	if(shift < 0) {
		array_extend(slice->array, shift); 
	}
}

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
