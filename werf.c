#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <X11/keysym.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>

#include "util.h"
#include "array.h"

#include "utf.h"
#include "font.h"
#include "edit.h"
#include "view.h"
#include "window.h"
#include "pipe.h"

static control_t *g_control;

static file_t file;
static view_wrap_t view_wrap = {
	.view.range.file = &file
};
static window_t win = {
	.width = 800,
	.height = 600,
	.view_wrap = &view_wrap
};

/*
typedef struct {
	size_t line_idx;
	size_t offset;
	double width;
} vis_to_line_t;

typedef struct {
	double width;
	size_t vis_first_idx;
} line_to_vis_t;
*/


int
selection_recv(control_t *control, void *usr, string_t *buf, size_t len)
{
	(void)usr;
	if(!len) {
		return 0;
	}
	if(control->pipe.disregard) {
		buf->nmemb -= len;
	}
	return 0;
}

int
control_recv(control_t *control, void *usr, string_t *buf, size_t len)
{
	static const char disregard_str[] = "disregard";
	static const char finish_str[] = "finish";

	(void)usr;
	if(!len) {
		return 0;
	}

	size_t shift = 0;

	char *delim;
	char *scan_start = buf->data + buf->nmemb - len;
	char *line_start = buf->data;
	while( (delim = memchr(scan_start, '\n', len)) ) {
		size_t line_len = delim - line_start;

		if(is_str_eq(line_start, line_len, disregard_str, sizeof disregard_str - 1)) {
			control->pipe.disregard = true;
		} else if(is_str_eq(line_start, line_len, finish_str, sizeof finish_str - 1)) {
			control->pipe.finish = true;
			if(control->pipe.write_end) {
				control->pipe.done = true;
			}
		} else {
			fprintf(stderr, "unknown ctl command: '%.*s'\n", (int)line_len, line_start);
		}

		scan_start = delim + 1;
		line_start = scan_start;

		len -= line_len + 1;
		shift += line_len + 1;
	}
	ARR_FRAG_SHIFT(buf, 0, buf->nmemb, -shift);
	ARR_SHRINK(buf, shift);
	return 0;
}

typedef struct {
	range_t rng;
	char buf[BUFSIZ * 2];
} selection_send_work_t;

int
selection_send(control_t *control, void *usr, string_t *buf, size_t len)
{
	selection_send_work_t *work = usr;
	if(!len) {
		control->pipe.write_end = true;
		if(control->pipe.finish) {
			control->pipe.done = true;
		}
		return 0;
	}
	buf->nmemb += range_copy(&work->rng, buf->data + buf->nmemb, buf->amemb - buf->nmemb);
	return 0;
}

int
handle_command(char *cmd)
{
	if(!strcmp("Delete", cmd)) {
		range_push(&win.view_wrap->view.range, "", 0, OP_Replace);
		return 0;
	}

	control_t control = {0};
	g_control = &control;

	selection_send_work_t selection_send_work = {
		.rng = win.view_wrap->view.range
	};

	struct {
		struct {
			pipe_t selection;
			pipe_t control;
		} r;
		struct {
			pipe_t selection;
		} w;
	} pipes = {
		.r.selection.handler = selection_recv,
		.r.control = {
			.child.name = "werf_control_W",
			.handler = control_recv
		},
		.w.selection = {
			.handler = selection_send,
			.usr = &selection_send_work,
			.buf.data = selection_send_work.buf,
			.buf.amemb = sizeof selection_send_work.buf
		}
	};

	pipe_t *pipes_all = (pipe_t*)&pipes;
	pipe_t *pipes_r = (pipe_t*)&pipes.r;
	pipe_t *pipes_w = (pipe_t*)&pipes.w;

	size_t num_r = sizeof pipes.r / sizeof pipes_r[0];
	size_t num_w = sizeof pipes.w / sizeof pipes_w[0];

	if(pipe_init(pipes_r, num_r, 0) < 0 ||
	pipe_init(pipes_w, num_w, 1) < 0) {
		return -1;
	}
	if( (control.child.pid = fork()) < 0 ) {
		goto out_err;
	}

	if(control.child.pid == 0) {
		char *argv[] = {"sh", "-c", cmd, (char*)0};
		if(dup2(pipes.w.selection.child.fd, STDIN_FILENO) < 0 ||
		dup2(pipes.r.selection.child.fd, STDOUT_FILENO) < 0) {
			die("dup2 failed: %s\n", strerror(errno));
		}
		pipe_cmd_exec(pipes_all, num_r + num_w, argv);
		die("pipe_cmd_exec failed: %s\n", strerror(errno));
	}

	for(size_t i = 0; i < num_r + num_w; i++) {
		close(pipes_all[i].child.fd);
		if(fcntl(pipes_all[i].fd, F_SETFL, O_NONBLOCK) < 0) {
			goto out_err;
		}
	}

	fd_set rfd;
	fd_set wfd;

	while(pipe_select(&control, &rfd, &wfd, pipes_r, num_r, pipes_w, num_w)) {
		for(size_t i = 0; i < num_r; i++) {
			if(pipes_r[i].fd >= 0 && FD_ISSET(pipes_r[i].fd, &rfd)) {
				pipe_recv(&pipes_r[i], &control);
				break;
			}
		}
		for(size_t i = 0; i < num_w; i++) {
			if(pipes_w[i].fd >= 0 && FD_ISSET(pipes_w[i].fd, &wfd)) {
				pipe_send(&pipes_w[i], &control);
				break;
			}
		}
	}

	for(size_t i = 0; i < num_r; i++) {
		while(pipes_r[i].fd >= 0) {
			pipe_recv(&pipes_r[i], &control);
		}
	}

	for(size_t i = 0; i < num_w; i++) {
		if(pipes_w[i].fd >= 0) {
			close(pipes_w[i].fd);
		}
	}

	if(!control.child.exited) {
		waitpid(control.child.pid, 0, 0);
	}
	g_control = 0;

	if(!control.pipe.disregard) {
		range_push(&win.view_wrap->view.range, pipes.r.selection.buf.data,
				pipes.r.selection.buf.nmemb, OP_Replace);
	}
	for(size_t i = 0; i < num_r; i++) {
		ARR_FREE(&pipes_r[i].buf);
	}

	if(control.pipe.disregard) {
		return 0;
	}
	return 1;

out_err:
	g_control = 0;
	for(size_t i = 0; i < num_r + num_w; i++) {
		if(pipes_all[i].fd >= 0) {
			close(pipes_all[i].fd);
		}
	}
	return -1;
}

void
file_read(file_t *f, char *fname)
{
	int fd = open(fname, O_RDONLY);
	DIEIF(fd < 0);

	range_read(&(range_t){.file = f}, fd);
	close(fd);

	f->dirty = true;

	printf("file lines: %zu\n", f->content.nmemb);
}

static void
sigchld(int sig, siginfo_t *inf, void *ctx)
{
	(void)sig;
	(void)ctx;

	if(g_control && !g_control->error && !g_control->child.exited &&
	g_control->child.pid == inf->si_pid) {
		g_control->child.exited = true;
		g_control->child.status = inf->si_status;
		if(inf->si_status) {
			g_control->error = true;
			g_control->pipe.done = true;
		}
		g_control = 0;
	}
}

int
main(int argc, char *argv[])
{
	setlocale(LC_CTYPE, "");
	signal(SIGPIPE, SIG_IGN);
	sigaction(SIGCHLD, &(struct sigaction) {
		.sa_sigaction = sigchld,
		.sa_flags = SA_SIGINFO | SA_NOCLDSTOP
	}, 0);

	file_insert_line(win.view_wrap->view.range.file, 0, "", 0);
	if(argc > 1) {
		file_read(&file, argv[1]);
	}

	window_init(&win);

	FT_Library ftlib;
	FT_Init_FreeType(&ftlib);
	DIEIF(!FcInit());

	fontset_t fontset = {0};
	DIEIF( fontset_init(&fontset, FcNameParse((FcChar8*)"DroidSans")) );
	cairo_font_face_t *font = font_cairo_font_face_create(&fontset);

	cairo_set_font_face(win.cr, font);
	//cairo_font_face_destroy(font);
	cairo_set_font_size(win.cr, 15.0);

	cairo_save(win.cr);
	double s = 0.625;
	cairo_matrix_t mat;
	cairo_get_font_matrix(win.cr, &mat);
	cairo_set_font_size(win.cr, mat.xx * s);

	char labels[] = "Cut\nCopy\nPaste\nDelete\nFind\n./Open\nExec\nurxvt\ngrep std | grep \"<.*>\" -o\n+\n...\n";
	char *lbl = labels;
	for(char *next; (next = strchr(lbl, '\n')) != NULL; lbl = next) {
		next++;
		toolbar_t *bar = &win.view_wrap->view.selbar_wrap.bar;
		ARR_EXTEND(&bar->buttons, 1);
		button_t *btn = &bar->buttons.data[bar->buttons.nmemb - 1];
		btn->label.data = lbl;
		btn->label.nmemb = next - lbl;
		btn->label.amemb = btn->label.nmemb;
		memset(&btn->glyphs, 0, sizeof btn->glyphs);

		glyphs_from_text(&btn->glyphs, cairo_get_scaled_font(win.cr),
				&btn->label);
		btn->label.data[btn->label.nmemb - 1] = '\0';
	}
	cairo_restore(win.cr);

	cairo_font_extents(win.cr, &win.view_wrap->view.extents);
	if(!win.view_wrap->view.font) {
		win.view_wrap->view.font = cairo_get_scaled_font(win.cr);
	}
	view_resize(&win.view_wrap->view, win.width, win.height);
	window_run(&win);

	fontset_free(&fontset);
	FcFini();
	FT_Done_FreeType(ftlib);

	window_deinit(&win);

	file_free(&file);

	for(size_t i = 0; i < win.view_wrap->view.nmemb; i++) {
		free(win.view_wrap->view.lines[i].data);
		free(win.view_wrap->view.lines[i].glyph_to_offset);
		free(win.view_wrap->view.lines[i].offset_to_glyph);
	}

	free(win.view_wrap->view.lines);

	return 0;
}
