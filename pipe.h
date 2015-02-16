typedef struct {
	int (*handler)(void *ctl, void *usr, string_t *buf, size_t len);
	void *usr;
	string_t buf;
} pipework_t;

typedef struct {
	int fd;
	struct {
		char *name;
		int fd;
	} child;
	pipework_t work;
} pipe_t;

pid_t pipe_spawn(char *argv[], pipe_t r_pipe[], size_t num_r_pipe,
		pipe_t w_pipe[], size_t num_w_pipe);

int pipe_loop(pid_t *pid, void *ctl, pipe_t r_pipe[], size_t num_r_pipe,
		pipe_t w_pipe[], size_t num_w_pipe);
