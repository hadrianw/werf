typedef ARRAY(char, buf) string_t;
typedef ARRAY(string_t, lines) dt_file_t;

void dt_file_insert_line(dt_file_t *f, size_t line, char *buf, size_t buf_len);
void dt_file_free(dt_file_t *f);

typedef struct {
	size_t line;
	size_t offset;
} dt_address_t;

int dt_address_cmp(dt_address_t *a1, dt_address_t *a2);

typedef struct {
	dt_address_t start;
	dt_address_t end;
	dt_file_t *file;
} dt_range_t;

int dt_range_from_addresses(dt_range_t *rng, dt_address_t *a1, dt_address_t *a2);
void dt_range_fix_start(dt_range_t *rng);
void dt_range_fix_end(dt_range_t *rng);
void dt_range_mod(dt_range_t *rng, char *mod_line, size_t mod_len);
int dt_range_read(dt_range_t *rng, int fd);

typedef struct undo_t undo_t;
struct undo_t {
	size_t prev;
	struct {
		dt_address_t start;
		dt_address_t end;
	} src, dst;
	size_t buf_len;
	char buf[];
};

typedef enum {
	OP_None,
	OP_BackSpace,
	OP_Delete,
	OP_Char,
	OP_Replace
} dt_optype_t;

typedef struct {
	size_t nsiz;
	size_t asiz;
	undo_t *first;
	size_t last;
	dt_optype_t last_type;
} undobuf_t;

void dt_range_push_mod(dt_range_t *rng, char *mod, size_t mod_len, undobuf_t *u, dt_optype_t type);
void dt_undo(undobuf_t *u, undobuf_t *r, dt_range_t *rng);
