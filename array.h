typedef struct {
	void *ptr;
	size_t nmemb;
	size_t amemb;
	size_t size;
} array_t;

typedef struct {
	array_t *array;
	ssize_t start;
	ssize_t end;
} slice_t;

typedef void (*slice_memb_func_t)(void *memb);

#define ARRAY(T, N) \
	union { \
		array_t array; \
		T *N; \
	}
#define INIT_ARRAY(X) {{.size = sizeof X[0]}}

void array_free(array_t *a);
void array_resize(array_t *a, size_t amemb);
void array_extend(array_t *a, ssize_t nchange);
void slice_apply(slice_t *slice, slice_memb_func_t func);
void slice_shift(slice_t *slice, ssize_t shift);
void slice_resize(slice_t *slice, size_t nmemb);
void *slice_get(slice_t *slice, ssize_t idx);
