typedef struct {
	char *name;
	int fd;
} pipedesc_t;

pid_t pipe_spawn(char *argv[], int read_fd[], pipedesc_t read_child_end[], size_t num_read_fd,
		int write_fd[], pipedesc_t write_child_end[], size_t num_write_fd);

typedef struct {
	int (*handler)(void *usr, string_t *buf, size_t *len);
	void *usr;
	string_t buf;
} pipework_t;

int pipe_loop(int read_fd[], pipework_t read_work[], size_t num_read_fd,
		int write_fd[], pipework_t write_work[], size_t num_write_fd);
