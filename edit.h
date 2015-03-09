typedef ARRAY(string_t) strarray_t;

typedef struct {
	size_t line;
	size_t offset;
} address_t;

typedef enum {
	OP_None,
	OP_BackSpace,
	OP_Delete,
	OP_Char,
	OP_Replace
} optype_t;

typedef struct {
	size_t prev;
	optype_t type;
	struct {
		address_t start;
		address_t end;
	} src, dst;
	size_t buf_len;
	char buf[];
} op_t;

typedef struct {
	size_t nsiz;
	size_t asiz;
	op_t *first;
	size_t last;
} opbuf_t;

typedef struct {
	strarray_t content;
	opbuf_t undobuf;
	opbuf_t redobuf;
	bool dirty;
} file_t;

typedef struct {
	address_t start;
	address_t end;
	file_t *file;
} range_t;

void file_insert_line(file_t *f, size_t line, char *buf, size_t buf_len);
void file_free(file_t *f);

int address_cmp(address_t *a1, address_t *a2);

int range_from_addresses(range_t *rng, address_t *a1, address_t *a2);
void range_fix_start(range_t *rng);
void range_fix_end(range_t *rng);
int range_read(range_t *rng, int fd);
size_t range_copy(range_t *rng, char *buf, size_t bufsiz);

void range_push(range_t *rng, char *mod, size_t mod_len, optype_t type);

void file_undo(range_t *rng);
void file_redo(range_t *rng);
