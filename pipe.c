#include <sys/types.h>
#include <sys/select.h>
#include <unistd.h>

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "util.h"
#include "array.h"

#include "pipe.h"

pid_t
pipe_spawn(char *argv[], int read_fd[], pipedesc_t read_child_end[], size_t num_read_fd,
		int write_fd[], pipedesc_t write_child_end[], size_t num_write_fd)
{
	static const char DEVFD[] = "/dev/fd/";

	pid_t pid = -1;
	size_t ri = 0;
	size_t wi = 0;

	for(; ri < num_read_fd; ri++) {
		int fds[2];
		if(pipe(fds) < 0) {
			pid = -errno;
			goto out;
		}
		read_fd[ri] = fds[0];
		read_child_end[ri].fd = fds[1];
	}
	for(; wi < num_write_fd; wi++) {
		int fds[2];
		if(pipe(fds) < 0) {
			pid = -errno;
			goto out;
		}
		write_fd[wi] = fds[1];
		write_child_end[wi].fd = fds[0];
	}

	pid = fork();
	if(pid < 0) {
		pid = -errno;
		goto out;
	}

	if(pid == 0) {
		for(size_t i = 0; i < num_write_fd; i++) {
			close(write_fd[i]);
		}
		for(size_t i = 0; i < num_read_fd; i++) {
			close(read_fd[i]);
		}

		dup2(write_child_end[0].fd, STDIN_FILENO);
		dup2(read_child_end[0].fd, STDOUT_FILENO);

		setenv("TERM", "dumb", 1);

		char buf[sizeof(DEVFD) + (sizeof(int) * 5 + 1) / 2];
		strcpy(buf, DEVFD);
		char *bufend = buf + sizeof DEVFD - 1;

		for(size_t i = 0; i < num_write_fd; i++) {
			if(write_child_end[i].name) {
				sprintf(bufend, "%d", write_child_end[i].fd);
				setenv(write_child_end[i].name, buf, 1);
			}
		}
		for(size_t i = 0; i < num_read_fd; i++) {
			if(read_child_end[i].name) {
				sprintf(bufend, "%d", read_child_end[i].fd);
				setenv(read_child_end[i].name, buf, 1);
			}
		}

		execvp(argv[0], argv);
		die("pipe_spawn execvp failed: %s\n", strerror(errno));
	}

out:
	for(size_t i = 0; i < wi; i++) {
		if(pid < 0) {
			close(write_fd[i]);
		}
		close(write_child_end[i].fd);
	}
	for(size_t i = 0; i < ri; i++) {
		if(pid < 0) {
			close(read_fd[i]);
		}
		close(read_child_end[i].fd);
	}
	return pid;
}

static void
pipe_send(int *fd, pipework_t *work)
{
	if(work->handler) {
		work->handler(work->usr, &work->buf, 0);
	}

	ssize_t len;
	if(work->buf.nmemb == 0 ||
			( (len = write(*fd, work->buf.data, work->buf.nmemb)) < 0 &&
			errno == EPIPE )) {
		close(*fd);
		*fd = -1;
	}

	if(len < 0) {
		perror("write failed");
		return;
	}

	ARR_FRAG_SHIFT(&work->buf, 0, work->buf.nmemb, -len);
	work->buf.nmemb -= len;
}

static void
pipe_recv(int *fd, pipework_t *work)
{
	enum { PIPE_BUF_SIZE = BUFSIZ * 2 };

	size_t start = work->buf.nmemb;
	ARR_EXTEND(&work->buf, PIPE_BUF_SIZE);

	ssize_t len = read(*fd, work->buf.data + start, PIPE_BUF_SIZE);
	if(len < 0) {
		perror("read failed");
		return;
	}

	work->buf.nmemb = start + len;

	if(work->handler) {
		work->handler(work->usr, &work->buf, &(size_t){len});
	}

	if(len == 0) {
		close(*fd);
		*fd = -1;
	}
}

int
pipe_loop(int read_fd[], pipework_t read_work[], size_t num_read_fd,
		int write_fd[], pipework_t write_work[], size_t num_write_fd)
{
	fd_set rfd;
	fd_set wfd;
	int max;

	for(;;) {
		FD_ZERO(&wfd);
		FD_ZERO(&rfd);
		max = -1;
		for(size_t i = 0; i < num_write_fd; i++) {
			if(write_fd[i] >= 0) {
				FD_SET(write_fd[i], &wfd);
				max = MAX(max, write_fd[i]);
			}
		}
		for(size_t i = 0; i < num_read_fd; i++) {
			if(read_fd[i] >= 0) {
				FD_SET(read_fd[i], &rfd);
				max = MAX(max, read_fd[i]);
			}
		}
		if(max < 0) {
			break;
		}

		DIEIF(pselect(max+1, &rfd, &wfd, NULL, NULL, NULL) < 0 && errno != EINTR);
		if(errno == EINTR) {
			continue;
		}

		for(size_t i = 0; i < num_write_fd; i++) {
			if(write_fd[i] >= 0 && FD_ISSET(write_fd[i], &wfd)) {
				pipe_send(&write_fd[i], &write_work[i]);
				break;
			}
		}
		for(size_t i = 0; i < num_read_fd; i++) {
			if(read_fd[i] >= 0 && FD_ISSET(read_fd[i], &rfd)) {
				pipe_recv(&read_fd[i], &read_work[i]);
				break;
			}
		}
	}
	return 0;
}
