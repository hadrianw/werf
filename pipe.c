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
pipe_spawn(char *argv[], pipe_t r_pipe[], size_t num_r_pipe,
		pipe_t w_pipe[], size_t num_w_pipe)
{
	static const char DEVFD[] = "/dev/fd/";

	pid_t pid = -1;
	size_t ri = 0;
	size_t wi = 0;

	for(; ri < num_r_pipe; ri++) {
		int fds[2];
		if(pipe(fds) < 0) {
			pid = -errno;
			goto out;
		}
		r_pipe[ri].fd = fds[0];
		r_pipe[ri].child.fd = fds[1];
	}
	for(; wi < num_w_pipe; wi++) {
		int fds[2];
		if(pipe(fds) < 0) {
			pid = -errno;
			goto out;
		}
		w_pipe[wi].fd = fds[1];
		w_pipe[wi].child.fd = fds[0];
	}

	pid = fork();
	if(pid < 0) {
		pid = -errno;
		goto out;
	}

	if(pid == 0) {
		for(size_t i = 0; i < num_w_pipe; i++) {
			close(w_pipe[i].fd);
		}
		for(size_t i = 0; i < num_r_pipe; i++) {
			close(r_pipe[i].fd);
		}

		dup2(w_pipe[0].child.fd, STDIN_FILENO);
		dup2(r_pipe[0].child.fd, STDOUT_FILENO);

		setenv("TERM", "dumb", 1);

		char buf[sizeof(DEVFD) + (sizeof(int) * 5 + 1) / 2];
		strcpy(buf, DEVFD);
		char *bufend = buf + sizeof DEVFD - 1;

		for(size_t i = 0; i < num_w_pipe; i++) {
			if(w_pipe[i].child.name) {
				sprintf(bufend, "%d", w_pipe[i].child.fd);
				setenv(w_pipe[i].child.name, buf, 1);
			}
		}
		for(size_t i = 0; i < num_r_pipe; i++) {
			if(r_pipe[i].child.name) {
				sprintf(bufend, "%d", r_pipe[i].child.fd);
				setenv(r_pipe[i].child.name, buf, 1);
			}
		}

		execvp(argv[0], argv);
		die("pipe_spawn execvp failed: %s\n", strerror(errno));
	}

out:
	for(size_t i = 0; i < wi; i++) {
		if(pid < 0) {
			close(w_pipe[i].fd);
		}
		close(w_pipe[i].child.fd);
	}
	for(size_t i = 0; i < ri; i++) {
		if(pid < 0) {
			close(r_pipe[i].fd);
		}
		close(r_pipe[i].child.fd);
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
		if(work->handler) {
			work->handler(work->usr, &work->buf, &(size_t){0});
		}
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
		if(errno != EAGAIN) {
			perror("read failed");
			return;
		}
		len = 0;
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
pipe_loop(pid_t *pid, pipe_t r_pipe[], size_t num_r_pipe, pipe_t w_pipe[], size_t num_w_pipe)
{
	fd_set rfd;
	fd_set wfd;
	int max = -1;

	for(; *pid > 0; max = -1) {
		FD_ZERO(&wfd);
		FD_ZERO(&rfd);
		for(size_t i = 0; i < num_w_pipe; i++) {
			if(w_pipe[i].fd >= 0) {
				FD_SET(w_pipe[i].fd, &wfd);
				max = MAX(max, w_pipe[i].fd);
			}
		}
		for(size_t i = 0; i < num_r_pipe; i++) {
			if(r_pipe[i].fd >= 0) {
				FD_SET(r_pipe[i].fd, &rfd);
				max = MAX(max, r_pipe[i].fd);
			}
		}
		if(max < 0) {
			break;
		}
		if(pselect(max+1, &rfd, &wfd, NULL, NULL, NULL) < 0) {
			if(errno == EINTR) {
				continue;
			}
			return -1;
		}

		for(size_t i = 0; i < num_w_pipe; i++) {
			if(w_pipe[i].fd >= 0 && FD_ISSET(w_pipe[i].fd, &wfd)) {
				pipe_send(&w_pipe[i].fd, &w_pipe[i].work);
				break;
			}
		}
		for(size_t i = 0; i < num_r_pipe; i++) {
			if(r_pipe[i].fd >= 0 && FD_ISSET(r_pipe[i].fd, &rfd)) {
				pipe_recv(&r_pipe[i].fd, &r_pipe[i].work);
				break;
			}
		}
	}
	bool repeat = max >= 0;
	while(repeat) {
		repeat = false;
		for(size_t i = 0; i < num_r_pipe; i++) {
			if(r_pipe[i].fd >= 0) {
				pipe_recv(&r_pipe[i].fd, &r_pipe[i].work);
				repeat = true;
			}
		}
	}
	return 0;
}
