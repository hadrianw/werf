typedef struct {
	void *ptr;
	size_t nmemb;
	size_t amemb;
} array_t;

typedef void (*array_memb_func_t)(void *memb);

#define ARRAY(T) \
	union { \
		array_t array; \
		struct { \
			T *data; \
			size_t nmemb; \
			size_t amemb; \
		}; \
	}

typedef ARRAY(char) string_t;

#define ARR_FREE(t_arr) \
	array_free(&(t_arr)->array)
#define ARR_REALLOC(t_arr, amemb) \
	array_realloc(&(t_arr)->array, sizeof((t_arr)->data[0]), amemb)
#define ARR_RESIZE(t_arr, nmemb) \
	array_extend(&(t_arr)->array, sizeof((t_arr)->data[0]), nmemb)
#define ARR_EXTEND(t_arr, nmore) \
	array_extend(&(t_arr)->array, sizeof((t_arr)->data[0]), nmore)
#define ARR_SHRINK(t_arr, nless) \
	array_shrink(&(t_arr)->array, sizeof((t_arr)->data[0]), nless)
#define ARR_FRAG_BOUNDS(t_arr, start, end, out_start, out_end) \
	array_fragment_bounds(&(t_arr)->array, in_start, in_end, out_start, out_end)
#define ARR_FRAG_APPLY(t_arr, start, end, func) \
	array_fragment_apply(&(t_arr)->array, sizeof((t_arr)->data[0]), start, end, func)
#define ARR_FRAG_SHIFT(t_arr, start, end, shift) \
	array_fragment_shift(&(t_arr)->array, sizeof((t_arr)->data[0]), start, end, shift)
#define ARR_FRAG_RESIZE(t_arr, start, end, nmemb) \
	array_fragment_resize(&(t_arr)->array, sizeof((t_arr)->data[0]), start, end, nmemb)

void array_free(array_t *a);
bool array_realloc(array_t *a, size_t size, size_t amemb);
bool array_resize(array_t *a, size_t size, size_t nmemb);
bool array_extend(array_t *a, size_t size, size_t nmore);
bool array_shrink(array_t *a, size_t size, size_t nless);
bool array_fragment_bounds(array_t *a, ssize_t start, ssize_t end,
		size_t *out_start, size_t *out_end);
void array_fragment_apply(array_t *a, size_t size, size_t start, size_t end,
		array_memb_func_t func);
void array_fragment_shift(array_t *a, size_t size, size_t start, size_t end,
		ssize_t shift);
bool array_fragment_resize(array_t *a, size_t size, size_t start, size_t end,
		size_t nmemb);
/*
void *slice_get(slice_t *slice, ssize_t idx);
*/
