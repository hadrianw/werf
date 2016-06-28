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
#include "edit.h"
#include "font.h"
#include "view.h"
#include "command.h"

ssize_t
view_clamp_start(view_t *v, ssize_t nr)
{
	return clampss(nr, -v->nmemb + 1, v->range.file->content.nmemb - 1);
}

void
view_set_start(view_t *v, size_t nr)
{
	ssize_t view_end = view_clamp_start(v, v->start + v->nmemb - 1);
	if((ssize_t)nr < v->start) {
		v->start = nr;
		v->range.file->dirty = true;
	} else if((ssize_t)nr > view_end) {
		v->start = nr - (v->nmemb - 1);
		v->range.file->dirty = true;
	}
}

glyphs_t *
view_get_glyphs(view_t *v, size_t nr)
{
	view_set_start(v, nr);
	if(v->range.file->dirty) {
		view_reshape(v);
	}

	return &v->lines[nr - v->start];
}

ssize_t
view_y_to_line(view_t *v, int y)
{
	return y / v->line_height + v->start;
}

size_t
view_x_to_offset(view_t *v, size_t nr, int x)
{
	glyphs_t *gl = view_get_glyphs(v, nr);

	if(x < v->left_margin) {
		return 0;
	}

	double prevx = 0.0;
	for(int i = 1; i < gl->nmemb; i++) {
		if(x < v->left_margin + prevx + (gl->data[i].x - prevx) * 0.5f) {
			return gl->glyph_to_offset[i - 1];
		}
		prevx = gl->data[i].x;
	}
	return gl->glyph_to_offset[gl->nmemb - 1];
}

double
view_address_to_x(view_t *v, address_t *adr) {
	glyphs_t *gl = view_get_glyphs(v, adr->line);
	return v->left_margin + gl->data[gl->offset_to_glyph[adr->offset]].x;
}

void
view_move_address_line(view_t *v, address_t *adr, int move)
{
	if(move > 0) {
		move = 1;
	} else if(move < 0) {
		move = -1;
	}
	if( (move < 0 && adr->line == 0) ||
	(move > 0 && adr->line == v->range.file->content.nmemb - 1) ) {
		view_set_start(v, adr->line);
		return;
	}

	adr->line += move;
	adr->offset = view_x_to_offset(v, adr->line, v->last_x);
}

void
view_move_address(view_t *v, address_t *adr, int move)
{
	if(!move) {
		return;
	}
	if(move > 0) {
		move = 1;
	} else {
		move = -1;
	}

	glyphs_t *gl = view_get_glyphs(v, adr->line);

	int gi = gl->offset_to_glyph[adr->offset];
	if( (move < 0 && gi > 0) || (move > 0 && gi < gl->nmemb - 1) ) {
		adr->offset = gl->glyph_to_offset[gi + move];
		return;
	}

	if( (move < 0 && adr->line == 0) ||
	(move > 0 && adr->line == v->range.file->content.nmemb - 1) ) {
		return;
	}

	adr->line += move;
	gl = view_get_glyphs(v, adr->line);
	adr->offset = gl->glyph_to_offset[move > 0 ? 0 : gl->nmemb - 1];
}

bool
view_move_start(view_t *v, ssize_t move)
{
	ssize_t new_start = view_clamp_start(v, v->start + move);

	if(new_start != v->start) {
		v->start = new_start;
		v->range.file->dirty = true;
		return true;
	}
	return false;
}

void
view_hide_toolbar(view_t *v, toolbar_wrap_t *bar_wrap, size_t line)
{
	if(!bar_wrap->visible) {
		return;
	}

	if(line > bar_wrap->line) {
		v->start--;
	}
	size_t start = clampss(v->start, 0, v->range.file->content.nmemb - 1);
	size_t end = view_clamp_start(v, v->start + v->nmemb - 1) + 1;
	if(bar_wrap->line >= start && bar_wrap->line <= end) {
		v->range.file->dirty = true;
	}
	bar_wrap->visible = false;
}

static void
glyphs_map(glyphs_t *gl, string_t *line)
{
	gl->glyph_to_offset = xrealloc(gl->glyph_to_offset, gl->nmemb,
			sizeof gl->glyph_to_offset[0]);
	gl->offset_to_glyph = xrealloc(gl->offset_to_glyph, line->nmemb,
			sizeof gl->offset_to_glyph[0]);

	char *utf8 = line->data;
	size_t len = line->nmemb;

	int gi = 0;
	size_t oi = 0;
	for(size_t chsiz = 0; (chsiz = utf8chsiz(utf8, len)) != 0 &&
			gi < gl->nmemb && oi < line->nmemb;
			utf8 += chsiz, len -= chsiz) {
		gl->glyph_to_offset[gi] = oi;
		gl->offset_to_glyph[oi] = gi;

		oi += chsiz;
		gi++;
	}
}

void
glyphs_from_text(glyphs_t *gl, cairo_scaled_font_t *font, string_t *line)
{
	if(line->nmemb > (size_t)gl->nmemb) {
		gl->nmemb = line->nmemb;
		gl->data = xrealloc(gl->data, gl->nmemb, sizeof gl->data[0]);
	}
	cairo_glyph_t *gl_initial = gl->data;

	bool last_line = line->nmemb == 0 || line->data[line->nmemb - 1] != '\n';
	if(last_line) {
		ARR_EXTEND(line, 1);
		line->data[line->nmemb - 1] = '\n';
	}

	font_text_to_glyphs(font, line->data, line->nmemb,
			&gl->data, &gl->nmemb, NULL, NULL, NULL);
	if(gl->data != gl_initial) {
		free(gl_initial);
	}

	cairo_matrix_t mat;
	cairo_scaled_font_get_font_matrix(font, &mat);
	for(int i = 0; i < gl->nmemb; i++) {
		gl->data[i].x *= mat.xx;
	}

	glyphs_map(gl, line);

	if(last_line) {
		line->nmemb--;
	}
}

void
view_xy_to_address(view_t *v, int x, int y, address_t *adr)
{
	adr->line = clampss(view_y_to_line(v, y), 0, v->range.file->content.nmemb - 1);
	adr->offset = view_x_to_offset(v, adr->line, x);
	v->last_x = view_address_to_x(v, adr);
}

// FIXME
bool handle_command(char *);

bool
toolbar_click(toolbar_t *bar, view_t *v, int x)
{
	double edge;
	for(size_t i = 0; i < bar->buttons.nmemb; i++) {
		button_t *btn = &bar->buttons.data[i];
		edge += v->left_margin + btn->glyphs.data[btn->glyphs.nmemb - 1].x +
				v->left_margin;
		if(x < edge) {
			// FIXME: what if I don't want to hide toolbar?
			return handle_command(btn->label.data);
		}
	}
	return true;
}

void
view_reshape(view_t *v)
{
	if(!v->nmemb) {
		return;
	}
	size_t start = clampss(v->start, 0, v->range.file->content.nmemb - 1);
	size_t end = view_clamp_start(v, v->start + v->nmemb - 1);
	for(size_t i = start; i <= end; i++) {
		string_t *line = &v->range.file->content.data[i];
		glyphs_t *gl = &v->lines[i - v->start];
		glyphs_from_text(gl, v->font, line);
	}
	v->range.file->dirty = false;
}

void
view_resize(view_t *v, int width, int height)
{
	v->width = width;
	v->height = height;
	v->line_height = v->extents.ascent + v->extents.descent;
	v->left_margin = v->extents.descent;

	size_t nmemb = v->height / v->line_height;
	if(nmemb == v->nmemb) {
		return;
	}

	for(size_t i = nmemb; i < v->nmemb; i++) {
		free(v->lines[i].data);
		free(v->lines[i].glyph_to_offset);
		free(v->lines[i].offset_to_glyph);
	}
	v->lines = xrealloc(v->lines, nmemb, sizeof v->lines[0]);
	for(size_t i = v->nmemb; i < nmemb; i++) {
		memset(v->lines + i, 0, sizeof v->lines[0]);
	}

	size_t view_end = view_clamp_start(v, v->start + v->nmemb - 1);
	size_t pivot;
	ssize_t vis_sel_start = v->start;
	ssize_t vis_sel_end = view_end;
	if(v->range.start.line <= view_end && (ssize_t)v->range.end.line >= v->start) {
		if((ssize_t)v->range.start.line > v->start) {
			vis_sel_start = v->range.start.line;
		}
		if(v->range.end.line < view_end) {
			vis_sel_end = v->range.end.line;
		}
	}
	pivot = vis_sel_end - (vis_sel_end - vis_sel_start) / 2;

	/* TODO: smooth scrolling
	if(v->nmemb > 0) {
		v->start = pivot - (pivot - v->start) * nmemb / v->nmemb;
printf(" p - (p-s) * N / n = %zu - (%zu-%zu) * %zu / %zu = %zu\n",
pivot, pivot, v->start, nmemb, v->nmemb, pivot - (pivot - v->start) * nmemb / v->nmemb);
		v->start = pivot - (pivot - v->start) * nmemb / v->nmemb;
	}
	*/

	v->range.file->dirty = true;
	v->nmemb = nmemb;
	view_set_start(v, pivot);
}

bool
view_keybinds(view_t *v, KeySym keysym)
{
	switch(keysym) {
	case XK_F2:
		command_undo(v);
		break;
	case XK_F3:
		command_redo(v);
		break;
	case XK_Page_Up:
		command_page_up(v);
		break;
	case XK_Page_Down:
		command_page_down(v);
		break;
	case XK_Home:
		command_home(v);
		break;
	case XK_End: {
		command_end(v);
		break;
	}
	case XK_BackSpace: {
		command_backspace(v);
		break;
	}
	case XK_Delete: {
		command_delete(v);
		break;
	}
	case XK_Return: /* FALL THROUGH */
	case XK_KP_Enter:
		command_new_line(v);
		break;
	case XK_Left:
		command_left(v);
		break;
	case XK_Right:
		command_right(v);
		break;
	case XK_Up:
		command_up(v);
		break;
	case XK_Down:
		command_down(v);
		break;
	default:
		return false;
	}

	return true;
}

bool
view_keypress(view_t *v, KeySym keysym, char *buf, size_t len)
{
	if(view_keybinds(v, keysym)) {
	} else if(len > 0) {
		range_push(&v->range, buf, len, OP_Char);
		v->last_x = view_address_to_x(v, &v->range.start);
	} else {
		return false;
	}

	view_hide_toolbar(v, &v->selbar_wrap, v->range.start.line);
	return true;
}

static int scroll_y = 0;
static bool selecting;
static address_t anchor;

bool
view_mouse_press(view_t *v, unsigned int btn, int x, int y)
{
	switch(btn) {
	case Button1: {
		ssize_t nr = view_y_to_line(v, y);
		int corr = 0;
		if(v->selbar_wrap.visible) {
			if(nr == (ssize_t)v->selbar_wrap.line) {
				// FIXME: what if I don't want to hide toolbar?
				if(toolbar_click(&v->selbar_wrap.bar, v, x)) {
					// FIXME: should I call view_hide_toolbar?
					v->selbar_wrap.visible = false;
					v->range.file->dirty = true;
				}
				return true;
			} else if(nr > (ssize_t)v->selbar_wrap.line) {
				corr = -1;
			}
		}

		selecting = true;
		anchor.line = clampss(nr + corr, 0, v->range.file->content.nmemb - 1);
		anchor.offset = view_x_to_offset(v, anchor.line, x);
		v->last_x = view_address_to_x(v, &anchor);
		v->range.start = anchor;
		v->range.end = anchor;

		view_hide_toolbar(v, &v->selbar_wrap, nr);
		return true;
		break;
	}
	case Button3:
		scroll_y = 0;
		break;
	case Button4:
		view_move_start(v, -3);
		return true;
	case Button5:
		view_move_start(v, 3);
		return true;
	}
	return false;
}

bool
view_mouse_motion(view_t *v, unsigned int state, int x, int y, int relx, int rely)
{
	(void)relx;

	if(state & Button1Mask) {
		if(!selecting) {
			return false;
		}
		address_t target;
		view_xy_to_address(v, x, y, &target);
		range_from_addresses(&v->range, &anchor, &target);
		return true;
	} else if(state & Button3Mask) {
		scroll_y -= rely;
		ssize_t move = scroll_y / v->line_height;
		scroll_y %= (int)v->line_height;
		return view_move_start(v, move);
	}

	return false;
}

bool
view_mouse_release(view_t *v, unsigned int btn, int x, int y)
{
	if(btn == Button1) {
		if(!selecting) {
			return false;
		}
		address_t target;
		view_xy_to_address(v, x, y, &target);
		int cmp = range_from_addresses(&v->range, &anchor, &target);
		if(cmp != 0) {
			v->selbar_wrap.visible = true;
			v->selbar_wrap.line = target.line;
			if(cmp < 0) {
				v->selbar_wrap.line++;
				view_move_start(v, 1);
			}
		}
		selecting = false;
		return true;
	}
	return false;
}

#if 0

int
main(int argc, char *argv[])
{
	setlocale(LC_CTYPE, "");
	signal(SIGPIPE, SIG_IGN);
	sigaction(SIGCHLD, &(struct sigaction) {
		.sa_sigaction = sigchld,
		.sa_flags = SA_SIGINFO | SA_NOCLDSTOP
	}, 0);

	file_insert_line(&file, 0, "", 0);

	if(argc > 1) {
		file_read(argv[1], &file);
	}

	window_init();

	FT_Library ftlib;
	FT_Init_FreeType(&ftlib);
	DIEIF(!FcInit());

	fontset_t fontset = {0};
	DIEIF( fontset_init(&fontset, FcNameParse((FcChar8*)"DroidSans")) );
	cairo_font_face_t *face = cairo_dt_face_create(&fontset);

	cairo_set_font_face(g.win.buf, face);
	cairo_font_face_destroy(face);
	cairo_set_font_size(g.win.buf, 15.0);

	view_resize();

/*
glyphs_t gl = {0};
for(size_t i = 0; i < file.nmemb; i++) {
	text_to_glyphs(&file.data[i], &gl);
}
return 0;
*/

	cairo_save(g.win.buf);
	double s = 0.625;
	cairo_matrix_t mat;
	cairo_get_font_matrix(g.win.buf, &mat);
	cairo_set_font_size(g.win.buf, mat.xx * s);

	char labels[] = "Cut\nCopy\nPaste\nDelete\nFind\n./Open\nExec\nurxvt\ngrep std | grep \"<.*>\" -o\n+\n...\n";
	char *lbl = labels;
	for(char *next; (next = strchr(lbl, '\n')) != NULL; lbl = next) {
		next++;
		ARR_EXTEND(&selbar.buttons, 1);
		button_t *btn = &selbar.buttons.data[selbar.buttons.nmemb - 1];
		btn->label.data = lbl;
		btn->label.nmemb = next - lbl;
		btn->label.amemb = btn->label.nmemb;
		memset(&btn->glyphs, 0, sizeof btn->glyphs);

		text_to_glyphs(&btn->label, &btn->glyphs);
		btn->label.data[btn->label.nmemb - 1] = '\0';
	}
	cairo_restore(g.win.buf);

	run();

	fontset_free(&fontset);
	FcFini();
	FT_Done_FreeType(ftlib);

	window_deinit();

	file_free(&file);

	for(size_t i = 0; i < g.view.nmemb; i++) {
		free(g.view.lines[i].data);
		free(g.view.lines[i].glyph_to_offset);
		free(g.view.lines[i].offset_to_glyph);
	}

	free(g.view.lines);

	return 0;
}

#endif
