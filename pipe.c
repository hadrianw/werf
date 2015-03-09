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

int
pipe_init(pipe_t *p, size_t num, int write)
{
	int err = 0;
	size_t n = 0;
	write = !!write;

	for(; n < num; n++) {
		int fds[2];
		if(pipe(fds) < 0) {
			err = errno;
			break;
		}
		p[n].fd = fds[write];
		p[n].child.fd = fds[!write];
	}
	if(err) {
		for(size_t i = 0; i <= n; i++) {
			close(p[i].fd);
			close(p[i].child.fd);
		}
		errno = err;
		return -1;
	}
	return 0;
}

int
pipe_set_env(pipe_t *p, size_t num)
{
	#define DEVFD "/dev/fd/"
	static char buf[sizeof(DEVFD) + (sizeof(int) * 5 + 1) / 2] = DEVFD;
	static char *bufend = buf + sizeof(DEVFD) - 1;

	for(size_t i = 0; i < num; i++) {
		if(p[i].child.name) {
			if(sprintf(bufend, "%d", p[i].child.fd) < 0 ||
			setenv(p[i].child.name, buf, 1) < 0) {
				return -1;
			}
		}
	}
	return 0;
}

int
pipe_cmd_exec(pipe_t *p, size_t num, char *argv[])
{
	for(size_t i = 0; i < num; i++) {
		close(p[i].fd);
	}

	if(setenv("TERM", "dumb", 1) < 0 ||
	pipe_set_env(p, num) < 0) {
		return -1;
	}

	execvp(argv[0], argv);
	return -1;
}

int
pipe_select(control_t *control, fd_set *rfd, fd_set *wfd,
		pipe_t *r, size_t num_r, pipe_t *w, size_t num_w)
{
	while(!control->pipe.done) {
		int max = -1;

		FD_ZERO(rfd);
		FD_ZERO(wfd);

		for(size_t i = 0; i < num_r; i++) {
			if(r[i].fd >= 0) {
				FD_SET(r[i].fd, rfd);
				max = MAX(max, r[i].fd);
			}
		}
		for(size_t i = 0; i < num_w; i++) {
			if(w[i].fd >= 0) {
				FD_SET(w[i].fd, wfd);
				max = MAX(max, w[i].fd);
			}
		}
		if(max < 0) {
			return 0;
		}

		if(pselect(max+1, rfd, wfd, NULL, NULL, NULL) < 0) {
			if(errno == EINTR) {
				continue;
			}
			control->error = 1;
			return 0;
		}
		return 1;
	}
	return 0;
}

void
pipe_send(pipe_t *p, control_t *ctl)
{
	if(p->handler) {
		p->handler(ctl, p->usr, &p->buf, 1);
	}

	ssize_t len;
	if(p->buf.nmemb == 0 ||
			( (len = write(p->fd, p->buf.data, p->buf.nmemb)) < 0 &&
			(errno == EPIPE || errno == EAGAIN) )) {
		if(p->handler) {
			p->handler(ctl, p->usr, &p->buf, 0);
		}
		close(p->fd);
		p->fd = -1;
		return;
	}

	if(len < 0) {
		perror("write failed");
		return;
	}

	ARR_FRAG_SHIFT(&p->buf, 0, p->buf.nmemb, -len);
	p->buf.nmemb -= len;
}

void
pipe_recv(pipe_t *p, control_t *ctl)
{
	enum { PIPE_BUF_SIZE = BUFSIZ * 2 };

	size_t start = p->buf.nmemb;
	ARR_EXTEND(&p->buf, PIPE_BUF_SIZE);

	ssize_t len = read(p->fd, p->buf.data + start, PIPE_BUF_SIZE);

	if(len < 0) {
		if(errno != EAGAIN) {
			perror("read failed");
			return;
		}
		len = 0;
	}

	p->buf.nmemb = start + len;

	if(p->handler) {
		p->handler(ctl, p->usr, &p->buf, len);
	}

	if(len == 0) {
		close(p->fd);
		p->fd = -1;
	}
}

