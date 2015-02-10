#include <sys/types.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <locale.h>
#include <time.h>

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

typedef struct {
	int width;
	int height;
	Display *display;
	int screen;
	GC gfxctx;
	Window window;
	Drawable pixmap;
	cairo_t *buf;
	XIM xim;
	XIC xic;
	bool run;
} windowing_t;

typedef struct {
	cairo_glyph_t *data;
	int nmemb;
	size_t *glyph_to_offset;
	int *offset_to_glyph;
} glyphs_t;

typedef struct {
	cairo_font_extents_t extents;
	double line_height;
	double left_margin;
	ssize_t start;
	glyphs_t *lines;
	size_t nmemb;
	int last_x;
} view_t;

static struct {
	windowing_t win;
	view_t view;
	dt_address_t anchor;
} g = {
	.win = {800, 600},
};

typedef struct {
	size_t line_idx;
	size_t offset;
	double width;
} vis_to_line_t;

typedef struct {
	double width;
	size_t vis_first_idx;
} line_to_vis_t;

void
windowing_init_input_methods()
{
	char *locale_mods[] = {
		"", 
		"@im=local",
		"@im=",
	};
	size_t i = 0;

	do {
		XSetLocaleModifiers(locale_mods[i++]);
		g.win.xim = XOpenIM(g.win.display, NULL, NULL, NULL);
	} while(g.win.xim == NULL && i < LEN(locale_mods));

	DIEIF(g.win.xim == NULL);

	g.win.xic = XCreateIC(g.win.xim, XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
			XNClientWindow, g.win.window, XNFocusWindow, g.win.window, NULL);
	DIEIF(g.win.xic == NULL);
}

void
windowing_init()
{
	g.win.display = XOpenDisplay(NULL);
	DIEIF(g.win.display == NULL);

	Window parent = XRootWindow(g.win.display, g.win.screen);
	g.win.screen = XDefaultScreen(g.win.display);
	g.win.window = XCreateSimpleWindow(g.win.display, parent,
			0, 0, g.win.width, g.win.height, 0, 0,
			WhitePixel(g.win.display, g.win.screen));

	g.win.gfxctx = XCreateGC(g.win.display, parent, GCGraphicsExposures,
			&(XGCValues){.graphics_exposures = False});
	XSetForeground(g.win.display, g.win.gfxctx, WhitePixel(g.win.display, g.win.screen));

	windowing_init_input_methods();

	XSelectInput(g.win.display, g.win.window, StructureNotifyMask | ExposureMask |
		KeyPressMask | ButtonMotionMask | ButtonPressMask | ButtonReleaseMask);
	XMapWindow(g.win.display, g.win.window);

	XSetWMProperties(g.win.display, g.win.window, NULL, NULL, NULL, 0, NULL,
			&(XWMHints){.flags = InputHint, .input = 1},
			&(XClassHint){"Werf", "Werf"});
	XSync(g.win.display, False);

	for(XEvent ev;;) {
		XNextEvent(g.win.display, &ev);
		if(XFilterEvent(&ev, None))
			continue;
		if(ev.type == ConfigureNotify) {
			g.win.width = ev.xconfigure.width;
			g.win.height = ev.xconfigure.height;
		} else if(ev.type == MapNotify) {
			break;
		}
	}

	g.win.pixmap = XCreatePixmap(g.win.display, g.win.window, g.win.width, g.win.height,
			DefaultDepth(g.win.display, g.win.screen));
	cairo_surface_t *bufsurf = cairo_xlib_surface_create(g.win.display, g.win.pixmap, 
			DefaultVisual(g.win.display, g.win.screen), g.win.width, g.win.height);
	g.win.buf = cairo_create(bufsurf);

	g.win.run = true;
}

void
windowing_deinit()
{
	cairo_destroy(g.win.buf);

	XCloseDisplay(g.win.display);
	g.win.display = NULL;
}

void reshape();

static dt_file_t file = INIT_ARRAY(file.lines);
static dt_range_t range = {.file = &file};
static undobuf_t undobuf;
static undobuf_t redobuf;
static bool dirty = true;

typedef struct {
	string_t label;
	glyphs_t glyphs;
} button_t;

typedef struct {
	size_t line;
	bool visible;
	ARRAY(button_t, data) buttons;
} toolbar_t;

static toolbar_t selbar = {.buttons = INIT_ARRAY(selbar.buttons.data)};

static ssize_t
clamp_view_start(ssize_t v)
{
	return clampss(v, -g.view.nmemb + 1, file.array.nmemb - 1);
}

void
view_to_line(size_t nr)
{
	ssize_t view_end = clamp_view_start(g.view.start + g.view.nmemb - 1);
	if((ssize_t)nr < g.view.start) {
		g.view.start = nr;
		reshape();
	} else if((ssize_t)nr > view_end) {
		g.view.start = nr - (g.view.nmemb - 1);
		reshape();
	}
	if(dirty) {
		reshape();
	}
}

ssize_t
y_to_line(int y)
{
	return y / g.view.line_height + g.view.start;
}

size_t
x_to_offset(size_t nr, int x)
{
	double margin = g.view.left_margin;

	view_to_line(nr);
	glyphs_t *gl = &g.view.lines[nr - g.view.start];

	if(x < margin) {
		return 0;
	}

	double prevx = 0.0;
	for(int i = 1; i < gl->nmemb; i++) {
		if(x < margin + prevx + (gl->data[i].x - prevx) * 0.5f) {
			return gl->glyph_to_offset[i - 1];
		}
		prevx = gl->data[i].x;
	}
	return gl->glyph_to_offset[gl->nmemb - 1];
}

double
address_to_x(dt_address_t *adr) {
	view_to_line(adr->line);
	glyphs_t *gl = &g.view.lines[adr->line - g.view.start];
	return g.view.left_margin + gl->data[gl->offset_to_glyph[adr->offset]].x;
}

void
address_move_line(dt_address_t *adr, dt_file_t *f, int move)
{
	view_to_line(adr->line);

	if(move > 0) {
		move = 1;
	} else if(move < 0) {
		move = -1;
	}
	if( (move < 0 && adr->line == 0) || (move > 0 && adr->line == f->array.nmemb - 1) ) {
		return;
	}

	adr->line += move;
	adr->offset = x_to_offset(adr->line, g.view.last_x);
}

void
address_move(dt_address_t *adr, dt_file_t *f, int move)
{
	view_to_line(adr->line);
	glyphs_t *gl = &g.view.lines[adr->line - g.view.start];

	int gi = gl->offset_to_glyph[adr->offset];
	int edge = 0;
	int next_edge = 0;
	if(move > 0) {
		move = 1;
		edge = gl->nmemb - 1;
	} else if(move < 0) {
		move = -1;
		next_edge = 1;
	}

	if(gi != edge) {
		adr->offset = gl->glyph_to_offset[gi + move];
		return;
	}

	if( (move < 0 && adr->line == 0) || (move > 0 && adr->line == f->array.nmemb - 1) ) {
		return;
	}

	adr->line += move;
	view_to_line(adr->line);
	gl = &g.view.lines[adr->line - g.view.start];
	next_edge *= gl->nmemb - 1;
	adr->offset = gl->glyph_to_offset[next_edge];
}

bool
view_move(ssize_t move)
{
	ssize_t new_start = clamp_view_start(g.view.start + move);

	if(new_start != g.view.start) {
		g.view.start = new_start;
		reshape();
		return true;
	}
	return false;
}

void
toolbar_hide(toolbar_t *bar, size_t line)
{
	if(!bar->visible) {
		return;
	}

	if(line > bar->line) {
		g.view.start--;
	}
	size_t start = clampss(g.view.start, 0, file.array.nmemb - 1);
	size_t end = clamp_view_start(g.view.start + g.view.nmemb - 1) + 1;
	if(bar->line >= start && bar->line <= end) {
		dirty = true;
	}
	bar->visible = false;
}

void
range_push_mod(dt_range_t *rng, char *mod, size_t mod_len, dt_optype_t type)
{
	redobuf.nsiz = 0;
	redobuf.last = 0;
	redobuf.last_type = OP_None;
	dt_range_push_mod(rng, mod, mod_len, &undobuf, type);
}

bool
keybinds(KeySym keysym)
{
	switch(keysym) {
	case XK_F2:
		dt_undo(&undobuf, &redobuf, &range);
		dirty = true;
		g.view.last_x = address_to_x(&range.start);
		break;
	case XK_F3:
		dt_undo(&redobuf, &undobuf, &range);
		dirty = true;
		g.view.last_x = address_to_x(&range.start);
		break;
	case XK_Page_Up:
		view_move(-g.view.nmemb);
		break;
	case XK_Page_Down:
		view_move(g.view.nmemb);
		break;
	case XK_Home:
		range.start.offset = 0;
		range.end.offset = range.start.offset;
		g.view.last_x = address_to_x(&range.start);
		break;
	case XK_End: {
		glyphs_t *gl = &g.view.lines[range.start.line - g.view.start];
		range.start.offset = gl->glyph_to_offset[gl->nmemb - 1];
		range.end.offset = range.start.offset;
		g.view.last_x = address_to_x(&range.start);
		break;
	}
	case XK_BackSpace: {
		dt_optype_t type = OP_Replace;
		if(!dt_address_cmp(&range.start, &range.end)) {
			address_move(&range.start, &file, -1);
			type = OP_BackSpace;
		}
		range_push_mod(&range, "", 0, type);
		dirty = true;
		g.view.last_x = address_to_x(&range.start);
		break;
	}
	case XK_Delete: {
		dt_optype_t type = OP_Replace;
		if(!dt_address_cmp(&range.start, &range.end)) {
			address_move(&range.end, &file, 1);
			type = OP_Delete;
		}
		range_push_mod(&range, "", 0, type);
		dirty = true;
		g.view.last_x = address_to_x(&range.start);
		break;
	}
	case XK_Return: /* FALL THROUGH */
	case XK_KP_Enter:
		range_push_mod(&range, "\n", 1, OP_Char);
		dirty = true;
		g.view.last_x = address_to_x(&range.start);
		break;
	case XK_Left:
		if(!dt_address_cmp(&range.start, &range.end)) {
			address_move(&range.start, &file, -1);
		}
		range.end = range.start;
		g.view.last_x = address_to_x(&range.start);
		break;
	case XK_Right:
		if(!dt_address_cmp(&range.start, &range.end)) {
			address_move(&range.end, &file, 1);
		}
		range.start = range.end;
		g.view.last_x = address_to_x(&range.start);
		break;
	case XK_Up:
		if(!dt_address_cmp(&range.start, &range.end)) {
			address_move_line(&range.start, &file, -1);
		}
		range.end = range.start;
		view_to_line(range.start.line);
		break;
	case XK_Down:
		if(!dt_address_cmp(&range.start, &range.end)) {
			address_move_line(&range.end, &file, 1);
		}
		range.start = range.end;
		view_to_line(range.start.line);
		break;
	case XK_Escape:
		g.win.run = false;
		break;
	default:
		return false;
	}

	return true;
}

bool
keypress(XEvent *ev)
{
	KeySym keysym;
	char buf[32];
	Status status;
	XKeyEvent *e = &ev->xkey;
	int len = XmbLookupString(g.win.xic, e, buf, sizeof buf, &keysym, &status);

	bool handled = keybinds(keysym);
	if(!handled && len > 0) {
		range_push_mod(&range, buf, len, OP_Char);
		dirty = true;
		g.view.last_x = address_to_x(&range.start);
		handled = true;
	}
	if(handled) {
		toolbar_hide(&selbar, range.start.line);
	}
	return handled;
}

void
glyph_map(string_t *line, glyphs_t *gl)
{
	gl->glyph_to_offset = xrealloc(gl->glyph_to_offset, gl->nmemb,
			sizeof gl->glyph_to_offset[0]);
	gl->offset_to_glyph = xrealloc(gl->offset_to_glyph, line->array.nmemb,
			sizeof gl->offset_to_glyph[0]);

	char *utf8 = line->buf;
	size_t len = line->array.nmemb;

	int gi = 0;
	size_t oi = 0;
	for(size_t chsiz = 0; (chsiz = utf8chsiz(utf8, len)) != 0 &&
			gi < gl->nmemb && oi < line->array.nmemb;
			utf8 += chsiz, len -= chsiz) {
		gl->glyph_to_offset[gi] = oi;
		gl->offset_to_glyph[oi] = gi;

		oi += chsiz;
		gi++;
	}

}

void
text_to_glyphs(string_t *line, glyphs_t *gl)
{
	if(line->array.nmemb > (size_t)gl->nmemb) {
		gl->nmemb = line->array.nmemb;
		gl->data = xrealloc(gl->data, gl->nmemb, sizeof gl->data[0]);
	}
	cairo_glyph_t *gl_initial = gl->data;

	bool last_line = line->array.nmemb == 0 || line->buf[line->array.nmemb - 1] != '\n';
	if(last_line) {
		array_extend(&line->array, 1);
		line->buf[line->array.nmemb - 1] = '\n';
	}

	cairo_scaled_font_t *sc = cairo_get_scaled_font(g.win.buf);

	dt_face_text_to_glyphs(sc, line->buf, line->array.nmemb,
			&gl->data, &gl->nmemb, NULL, NULL, NULL);
	if(gl->data != gl_initial) {
		free(gl_initial);
	}

	cairo_matrix_t mat;
	cairo_scaled_font_get_font_matrix(sc, &mat);
	for(int i = 0; i < gl->nmemb; i++) {
		gl->data[i].x *= mat.xx;
	}

	glyph_map(line, gl);

	if(last_line) {
		line->array.nmemb--;
	}
}

void
xy_to_address(int x, int y, dt_address_t *adr)
{
	adr->line = clampss(y_to_line(y), 0, file.array.nmemb - 1);
	adr->offset = x_to_offset(adr->line, x);
	g.view.last_x = address_to_x(adr);
}

static int scroll_y = 0;
static int prevx, prevy;
static bool selecting;

enum {
	P2CH_SEL,
	NUM_P2CH
};

char *p2ch_names[NUM_P2CH] = {
	[P2CH_SEL] = 0
};

enum {
	CH2P_SEL,
	CH2P_CTL,
	NUM_CH2P
};

char *ch2p_names[NUM_CH2P] = {
	[CH2P_SEL] = 0,
	[CH2P_CTL] = "werf_control_W"
};

void
spawn_command(char *cmd)
{
	puts(cmd);
	if(!strcmp("Delete", cmd)) {
		range_push_mod(&range, "", 0, OP_Replace);
		dirty = true;
		return;
	}

	int parent_to_child[NUM_P2CH][2];
	int child_to_parent[NUM_CH2P][2];

	for(size_t i = 0; i < LEN(parent_to_child); i++) {
		DIEIF(pipe(parent_to_child[i]) == -1);
	}
	for(size_t i = 0; i < LEN(child_to_parent); i++) {
		DIEIF(pipe(child_to_parent[i]) == -1);
	}

	pid_t pid = fork();
	DIEIF(pid < 0);

	if(pid == 0) {
		for(size_t i = 0; i < LEN(parent_to_child); i++) {
			close(parent_to_child[i][1]);
		}
		for(size_t i = 0; i < LEN(child_to_parent); i++) {
			close(child_to_parent[i][0]);
		}

		dup2(parent_to_child[P2CH_SEL][0], STDIN_FILENO);
		dup2(child_to_parent[CH2P_SEL][1], STDOUT_FILENO);

		// TODO: env[TERM] = dumb

		for(size_t i = 0; i < LEN(parent_to_child); i++) {
			// TODO: env[name] = /dev/fd/NUM
		}
		for(size_t i = 0; i < LEN(child_to_parent); i++) {
			// TODO: env[name] = /dev/fd/NUM
		}

		char *args[] = {"sh", "-c", cmd, (char*)0};
		execvp(args[0], args);
		die("execvp failed %s\n", strerror(errno));
	}

	for(size_t i = 0; i < LEN(parent_to_child); i++) {
		close(parent_to_child[i][0]);
		fcntl(parent_to_child[i][1], F_SETFL, O_NONBLOCK);
	}
	for(size_t i = 0; i < LEN(child_to_parent); i++) {
		close(child_to_parent[i][1]);
		fcntl(child_to_parent[i][0], F_SETFL, O_NONBLOCK);
	}

	fd_set wfd;
	fd_set rfd;
	int max;

	enum { PIPE_BUF_SIZE = BUFSIZ * 2 };

	struct {
		char buf[PIPE_BUF_SIZE];
		size_t buf_len;
		size_t write_len;
		dt_range_t rng;
	} sel_write;
	sel_write.buf_len = 0;
	sel_write.write_len = 0;
	sel_write.rng = range;

	string_t sel_read = INIT_ARRAY(sel_read.buf);
	bool disregard = false;

	for(;;) {
		FD_ZERO(&wfd);
		FD_ZERO(&rfd);
		max = -1;
		for(size_t i = 0; i < LEN(parent_to_child); i++) {
			if(parent_to_child[i][1] >= 0) {
				FD_SET(parent_to_child[i][1], &wfd);
				max = MAX(max, parent_to_child[i][1]);
			}
		}
		for(size_t i = 0; i < LEN(child_to_parent); i++) {
			if(child_to_parent[i][0] >= 0) {
				FD_SET(child_to_parent[i][0], &rfd);
				max = MAX(max, child_to_parent[i][0]);
			}
		}
		if(max < 0) {
			pid = 0;
			break;
		}

		DIEIF(pselect(max+1, &rfd, &wfd, NULL, NULL, NULL) < 0 && errno != EINTR);
		if(errno == EINTR) {
			continue;
		}

		if(child_to_parent[CH2P_CTL][0] >= 0 &&
				FD_ISSET(child_to_parent[CH2P_CTL][0], &rfd)) {
			struct { char data[BUFSIZ]; char null; } buf;
			ssize_t len = read(child_to_parent[CH2P_CTL][0], buf.data, sizeof buf.data);
			if(len >= 0) {
				buf.data[len] = 0;
				if(!strcmp(buf.data, "disregard\n")) {
					disregard = true;
					close(child_to_parent[CH2P_SEL][0]);
					child_to_parent[CH2P_SEL][0] = -1;
					array_free(&sel_read.array);
				} else if(len > 0) {
					fputs("unknown ctl command\n", stderr);
					len = 0;
				}
				if(len == 0) {
					close(child_to_parent[CH2P_CTL][0]);
					child_to_parent[CH2P_CTL][0] = -1;
				}
			}
		} else if(parent_to_child[P2CH_SEL][1] >= 0 &&
				FD_ISSET(parent_to_child[P2CH_SEL][1], &wfd)) {
			memshift(-sel_write.write_len, sel_write.buf, sel_write.buf_len,
					sizeof sel_write.buf[0]);
			sel_write.buf_len -= sel_write.write_len;

			sel_write.buf_len += dt_range_copy(&sel_write.rng,
					sel_write.buf + sel_write.buf_len,
					sizeof sel_write.buf - sel_write.buf_len);
			if(sel_write.buf_len > 0) {
				ssize_t len = write(parent_to_child[P2CH_SEL][1],
						sel_write.buf, sel_write.buf_len);
				if(len < 0 && errno == EPIPE) {
					len = 0;
					sel_write.buf_len = 0;
					sel_write.rng.start = sel_write.rng.end;
				}
				if(len >= 0) {
					sel_write.write_len = len;
				} else {
					perror("write failed");
					sel_write.write_len = 0;
				}
			} else {
				sel_write.write_len = 0;
			}

			if(sel_write.write_len == sel_write.buf_len &&
					!dt_address_cmp(&sel_write.rng.start, &sel_write.rng.end)) {
				close(parent_to_child[P2CH_SEL][1]);
				parent_to_child[P2CH_SEL][1] = -1;
			}
		} else if(child_to_parent[CH2P_SEL][0] >= 0 &&
				FD_ISSET(child_to_parent[CH2P_SEL][0], &rfd)) {
			size_t start = sel_read.array.nmemb;
			array_extend(&sel_read.array, PIPE_BUF_SIZE);

			ssize_t len = read(child_to_parent[CH2P_SEL][0],
					sel_read.buf + start, PIPE_BUF_SIZE);
			if(len < 0 && errno == EPIPE) {
				len = 0;
			}
			if(len >= 0) {
				sel_read.array.nmemb = start + len;
				if(len == 0) {
					close(child_to_parent[CH2P_SEL][0]);
					child_to_parent[CH2P_SEL][0] = -1;
				}
			} else {
				perror("read failed");
			}
		}
	}

	for(size_t i = 0; i < LEN(parent_to_child); i++) {
		if(parent_to_child[i][1] >= 0) {
			close(parent_to_child[i][1]);
		}
	}
	for(size_t i = 0; i < LEN(child_to_parent); i++) {
		if(child_to_parent[i][0] >= 0) {
			close(child_to_parent[i][0]);
		}
	}

	if(!disregard) {
		range_push_mod(&range, sel_read.buf, sel_read.array.nmemb, OP_Replace);
		array_free(&sel_read.array);
	}
}

void
toolbar_click(toolbar_t *bar, int x)
{
	double edge = g.view.left_margin;
	for(size_t i = 0; i < bar->buttons.array.nmemb; i++) {
		button_t *btn = &bar->buttons.data[i];
		edge += btn->glyphs.data[btn->glyphs.nmemb - 1].x +
			g.view.left_margin;
		if(x < edge) {
			spawn_command(btn->label.buf);
			break;
		}
		edge += g.view.left_margin;
	}
}

bool
mouse_press(XEvent *ev)
{
	XButtonEvent *e = &ev->xbutton;
	prevx = e->x;
	prevy = e->y;
	switch(e->button) {
	case Button1: {
		ssize_t nr = y_to_line(e->y);
		int corr = 0;
		if(selbar.visible) {
			if(nr == (ssize_t)selbar.line) {
				toolbar_click(&selbar, e->x);
				selbar.visible = false;
				dirty = true;
				return true;
			} else if(nr > (ssize_t)selbar.line) {
				corr = -1;
			}
		}

		selecting = true;
		g.anchor.line = clampss(nr + corr, 0, file.array.nmemb - 1);
		g.anchor.offset = x_to_offset(g.anchor.line, e->x);
		g.view.last_x = address_to_x(&g.anchor);
		range.start = g.anchor;
		range.end = g.anchor;

		toolbar_hide(&selbar, nr);
		return true;
		break;
	}
	case Button3:
		scroll_y = 0;
		break;
	case Button4:
		view_move(-3);
		return true;
	case Button5:
		view_move(3);
		return true;
	}
	return false;
}

bool
mouse_motion(XEvent *ev)
{
	bool handled = false;
	XMotionEvent *e = &ev->xmotion;
	if(e->state & Button1Mask) {
		if(!selecting) {
			return false;
		}
		dt_address_t target;
		xy_to_address(e->x, e->y, &target);
		dt_range_from_addresses(&range, &g.anchor, &target);
		handled = true;
	} else if(e->state & Button3Mask) {
		scroll_y += prevy - e->y;
		ssize_t move = scroll_y / g.view.line_height;
		scroll_y %= (int)g.view.line_height;
		handled = view_move(move);
	}

	prevx = e->x;
	prevy = e->y;
	return handled;
}

bool
mouse_release(XEvent *ev)
{
	XButtonEvent *e = &ev->xbutton;
	if(e->button == Button1) {
		if(!selecting) {
			return false;
		}
		dt_address_t target;
		xy_to_address(e->x, e->y, &target);
		int cmp = dt_range_from_addresses(&range, &g.anchor, &target);
		if(cmp != 0) {
			selbar.visible = true;
			selbar.line = target.line;
			if(cmp < 0) {
				selbar.line++;
				g.view.start++;
				dirty = true;
			}
		}
		selecting = false;
		return true;
	}
	return false;
}

void
draw_cursor(dt_address_t *adr)
{
	double x = address_to_x(adr);

	cairo_save(g.win.buf);
	cairo_set_antialias(g.win.buf, CAIRO_ANTIALIAS_NONE);

	double ds = g.view.extents.descent;
	if(ds > 0) {
		ds--;
	}

	cairo_move_to(g.win.buf, x, 1);
	cairo_line_to(g.win.buf, x, g.view.line_height - 1);

	cairo_move_to(g.win.buf, x-ds*0.5, 1);
	cairo_line_to(g.win.buf, x+ds*0.5, 1);

	cairo_move_to(g.win.buf, x-ds*0.5, g.view.line_height - 1);
	cairo_line_to(g.win.buf, x+ds*0.5, g.view.line_height - 1);

	cairo_set_line_width(g.win.buf, 1);
	cairo_stroke(g.win.buf);

	cairo_restore(g.win.buf);
}

void
draw_line(size_t nr)
{
	glyphs_t *gl = &g.view.lines[nr - g.view.start];

	cairo_save(g.win.buf);

	double yoff = g.view.line_height * (nr - g.view.start);
	cairo_translate(g.win.buf, 0, yoff);

	double sel_s = 0;
	double sel_e = 0;

	if(nr == range.start.line) {
		sel_s = address_to_x(&range.start);
	}
	if(nr == range.end.line) {
		sel_e = address_to_x(&range.end);
	} else if(nr >= range.start.line && nr < range.end.line) {
		sel_e = g.win.width;
	}

	cairo_save(g.win.buf);
	cairo_set_source_rgb(g.win.buf, 0.625, 0.75, 1);
	cairo_rectangle(g.win.buf, sel_s, 0, sel_e - sel_s, g.view.line_height);
	cairo_clip(g.win.buf);
	cairo_paint(g.win.buf);
	cairo_restore(g.win.buf);

	if(nr == range.start.line && !dt_address_cmp(&range.start, &range.end)) {
		draw_cursor(&range.start);
	}

	cairo_translate(g.win.buf, g.view.left_margin, g.view.extents.ascent);
	cairo_show_glyphs(g.win.buf, gl->data, gl->nmemb);

	cairo_restore(g.win.buf);
}

void
draw_button(button_t *btn)
{
	cairo_translate(g.win.buf, g.view.left_margin, 0);

	cairo_save(g.win.buf);
	cairo_translate(g.win.buf, 0, g.view.extents.ascent);
	cairo_show_glyphs(g.win.buf, btn->glyphs.data, btn->glyphs.nmemb);
	cairo_restore(g.win.buf);

	cairo_translate(g.win.buf, btn->glyphs.data[btn->glyphs.nmemb - 1].x +
			g.view.left_margin, 0);

	cairo_move_to(g.win.buf, 0, 0);
	cairo_line_to(g.win.buf, 0, g.view.line_height);
	cairo_set_line_width(g.win.buf, 1);
	cairo_stroke(g.win.buf);
}

void
draw_toolbar(toolbar_t *bar)
{
	cairo_save(g.win.buf);

	double yoff = g.view.line_height * (bar->line - g.view.start);
	cairo_translate(g.win.buf, 0, yoff);

	cairo_save(g.win.buf);
	cairo_set_source_rgb(g.win.buf, 0.875, 0.75, 0.5);
	// salmon //cairo_set_source_rgb(g.win.buf, 1, 0.5, 0.5);
	// yellow green // cairo_set_source_rgb(g.win.buf, 0.625, 0.75, 0.25);
	cairo_rectangle(g.win.buf, 0, 0, g.win.width, g.view.line_height);
	cairo_clip(g.win.buf);
	cairo_paint(g.win.buf);

	cairo_move_to(g.win.buf, 0, 0);
	cairo_line_to(g.win.buf, g.win.width, 0);
	cairo_move_to(g.win.buf, 0, g.view.line_height);
	cairo_line_to(g.win.buf, g.win.width, g.view.line_height);
	cairo_set_line_width(g.win.buf, 1);
	cairo_set_source_rgb(g.win.buf, 0, 0, 0);
	cairo_stroke(g.win.buf);

	cairo_restore(g.win.buf);

	double s = 0.625;
	cairo_matrix_t mat;
	cairo_get_font_matrix(g.win.buf, &mat);
	cairo_set_font_size(g.win.buf, mat.xx * s);

	for(size_t i = 0; i < bar->buttons.array.nmemb; i++) {
		draw_button(&bar->buttons.data[i]);
	}

	cairo_restore(g.win.buf);
	cairo_translate(g.win.buf, 0, g.view.line_height);
}

void
reshape()
{
	size_t start = clampss(g.view.start, 0, file.array.nmemb - 1);
	size_t end = clamp_view_start(g.view.start + g.view.nmemb - 1);
	for(size_t i = start; i <= end; i++) {
		string_t *line = &file.lines[i];
		glyphs_t *gl = &g.view.lines[i - g.view.start];
		text_to_glyphs(line, gl);
	}
	dirty = false;
}

void
redraw()
{
	XFillRectangle(g.win.display, g.win.pixmap, g.win.gfxctx,
			0, 0, g.win.width, g.win.height);

	cairo_identity_matrix(g.win.buf);
	cairo_set_source_rgba(g.win.buf, 0, 0, 0, 1);

	size_t start = clampss(g.view.start, 0, file.array.nmemb - 1);
	size_t end = clamp_view_start(g.view.start + g.view.nmemb - 1);
	size_t i = start;
	for(; i <= end; i++) {
		if(selbar.visible && i == selbar.line) {
			draw_toolbar(&selbar);
		}
		draw_line(i);
	}
	if(selbar.visible && i == selbar.line) {
		draw_toolbar(&selbar);
	}
	
	XCopyArea(g.win.display, g.win.pixmap, g.win.window, g.win.gfxctx,
			0, 0, g.win.width, g.win.height, 0, 0);
}

void
view_resize()
{
	cairo_font_extents(g.win.buf, &g.view.extents);
	g.view.line_height = g.view.extents.ascent + g.view.extents.descent;
	g.view.left_margin = g.view.extents.descent;

	size_t nmemb = g.win.height / g.view.line_height;
	if(nmemb == g.view.nmemb) {
		return;
	}

	for(size_t i = nmemb; i < g.view.nmemb; i++) {
		free(g.view.lines[i].data);
		free(g.view.lines[i].glyph_to_offset);
		free(g.view.lines[i].offset_to_glyph);
	}
	g.view.lines = xrealloc(g.view.lines, nmemb, sizeof g.view.lines[0]);
	for(size_t i = g.view.nmemb; i < nmemb; i++) {
		memset(g.view.lines + i, 0, sizeof g.view.lines[0]);
	}

	size_t view_end = clamp_view_start(g.view.start + g.view.nmemb - 1);
	size_t pivot;
	ssize_t vis_sel_start = g.view.start;
	ssize_t vis_sel_end = view_end;
	if(range.start.line <= view_end && (ssize_t)range.end.line >= g.view.start) {
		if((ssize_t)range.start.line > g.view.start) {
			vis_sel_start = range.start.line;
		}
		if(range.end.line < view_end) {
			vis_sel_end = range.end.line;
		}
	}
	pivot = vis_sel_end - (vis_sel_end - vis_sel_start) / 2;

	/* TODO: smooth scrolling
	if(g.view.nmemb > 0) {
		g.view.start = pivot - (pivot - g.view.start) * nmemb / g.view.nmemb;
printf(" p - (p-s) * N / n = %zu - (%zu-%zu) * %zu / %zu = %zu\n",
pivot, pivot, g.view.start, nmemb, g.view.nmemb, pivot - (pivot - g.view.start) * nmemb / g.view.nmemb);
		g.view.start = pivot - (pivot - g.view.start) * nmemb / g.view.nmemb;
	}
	*/

	dirty = true;
	g.view.nmemb = nmemb;
	view_to_line(pivot);
}

bool
resize(XEvent *ev)
{
	if(g.win.width == ev->xconfigure.width && g.win.height == ev->xconfigure.height) {
		return false;
	}
	g.win.width = ev->xconfigure.width;
	g.win.height = ev->xconfigure.height;

	XFreePixmap(g.win.display, g.win.pixmap);
	g.win.pixmap = XCreatePixmap(g.win.display, g.win.window, g.win.width, g.win.height,
			DefaultDepth(g.win.display, g.win.screen));

	cairo_xlib_surface_set_drawable(cairo_get_target(g.win.buf),
			g.win.pixmap, g.win.width, g.win.height);

	view_resize();
	return true;
}

void
run()
{
	fd_set rfd;
	int xfd = XConnectionNumber(g.win.display);
	struct timespec now, prev;
	struct timespec drawtime = {.tv_nsec = 0};
	struct timespec *tv = &drawtime;
	bool draw_request = false;

	clock_gettime(CLOCK_MONOTONIC, &prev);
	XEvent ev;
	while(g.win.run) {
		FD_ZERO(&rfd);
		FD_SET(xfd, &rfd);
		DIEIF(pselect(xfd+1, &rfd, NULL, NULL, tv, NULL) < 0 && errno != EINTR);

		while(XPending(g.win.display)) {
			bool handled = false;
			XNextEvent(g.win.display, &ev);
			switch(ev.type) {
			case KeyPress:
				handled = keypress(&ev);
				break;
			case ButtonPress:
				handled = mouse_press(&ev);
				break;
			case ButtonRelease:
				handled = mouse_release(&ev);
				break;
			case MotionNotify:
				handled = mouse_motion(&ev);
				break;
			case ConfigureNotify:
				handled = resize(&ev);
				break;
			case Expose:
				handled = true;
				break;
			default:
				break;
			}
			if(handled) {
				draw_request = true;
			}
			if(dirty) {
				reshape();
			}
		}

		if(!draw_request) {
			continue;
		}
		clock_gettime(CLOCK_MONOTONIC, &now);
		unsigned int diff = ((now.tv_sec - prev.tv_sec) * 1000 +
				(now.tv_nsec - prev.tv_nsec) / 1E6);
		if(diff < 1000 / 60) {
			drawtime.tv_nsec = diff * 1E6;
			tv = &drawtime;
			continue;
		}

		draw_request = false;
		redraw();
		XFlush(g.win.display);
		clock_gettime(CLOCK_MONOTONIC, &prev);
		tv = NULL;
	}
}

void
file_read(char *fname, dt_file_t *f)
{
	int fd = open(fname, O_RDONLY);
	DIEIF(fd < 0);

	dt_range_read(&(dt_range_t){.file = f}, fd);
	close(fd);

	printf("file lines: %zu\n", f->array.nmemb);
}

int
main(int argc, char *argv[])
{
	setlocale(LC_CTYPE, "");
	signal(SIGPIPE, SIG_IGN);
	dt_file_insert_line(&file, 0, "", 0);

	if(argc > 1) {
		file_read(argv[1], &file);
	}

	windowing_init();

	FT_Library ftlib;
	FT_Init_FreeType(&ftlib);
	DIEIF(!FcInit());

	fontset_t fontset = {0};
	DIEIF( fontset_init(&fontset, FcNameParse((FcChar8*)"DroidSans")) );
	cairo_font_face_t *dt_face = cairo_dt_face_create(&fontset);

	cairo_set_font_face(g.win.buf, dt_face);
	cairo_font_face_destroy(dt_face);
	cairo_set_font_size(g.win.buf, 15.0);

	view_resize();

	cairo_save(g.win.buf);
	double s = 0.625;
	cairo_matrix_t mat;
	cairo_get_font_matrix(g.win.buf, &mat);
	cairo_set_font_size(g.win.buf, mat.xx * s);

	char labels[] = "Cut\nCopy\nPaste\nDelete\nFind\nOpen\nExec\n+\n...\n";
	char *lbl = labels;
	for(char *next; (next = strchr(lbl, '\n')) != NULL; lbl = next) {
		next++;
		array_extend(&selbar.buttons.array, 1);
		button_t *btn = &selbar.buttons.data[selbar.buttons.array.nmemb - 1];
		btn->label.buf = lbl;
		btn->label.array.nmemb = next - lbl;
		btn->label.array.amemb = btn->label.array.nmemb;
		btn->label.array.size = 1;
		memset(&btn->glyphs, 0, sizeof btn->glyphs);

		text_to_glyphs(&btn->label, &btn->glyphs);
		btn->label.buf[btn->label.array.nmemb - 1] = '\0';
	}
	cairo_restore(g.win.buf);

	run();

	fontset_free(&fontset);
	FcFini();
	FT_Done_FreeType(ftlib);

	windowing_deinit();

	dt_file_free(&file);

	for(size_t i = 0; i < g.view.nmemb; i++) {
		free(g.view.lines[i].data);
		free(g.view.lines[i].glyph_to_offset);
		free(g.view.lines[i].offset_to_glyph);
	}

	free(g.view.lines);

	return 0;
}
