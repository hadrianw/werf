typedef struct {
	struct {
		bool disregard;
		bool finish;
		bool write_end;
		bool done;
	} pipe;
	struct {
		bool exited;
		int status;
		pid_t pid;
	} child;
	bool error;
} control_t;

typedef struct {
	int fd;
	int (*handler)(control_t *control, void *usr, string_t *buf, size_t len);
	void *usr;
	string_t buf;

	struct {
		char *name;
		int fd;
	} child;
} pipe_t;

int pipe_init(pipe_t *p, size_t num, int write);
int pipe_set_env(pipe_t *p, size_t num);
int pipe_cmd_exec(pipe_t *p, size_t num, char *argv[]);
int pipe_select(control_t *control, fd_set *rfd, fd_set *wfd,
		pipe_t *r, size_t num_r, pipe_t *w, size_t num_w);
void pipe_send(pipe_t *p, control_t *ctl);
void pipe_recv(pipe_t *p, control_t *ctl);
