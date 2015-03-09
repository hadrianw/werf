#include <cairo/cairo.h>

#include <X11/Xlib.h>

#include "util.h"
#include "array.h"

#include "edit.h"
#include "view.h"
#include "draw.h"

void
draw_cursor(cairo_t *cr, view_t *v, address_t *adr)
{
	double x = view_address_to_x(v, adr);

	cairo_save(cr);
	cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);

	double ds = v->extents.descent;
	if(ds > 0) {
		ds--;
	}

	cairo_move_to(cr, x, 1);
	cairo_line_to(cr, x, v->line_height - 1);

	cairo_move_to(cr, x-ds*0.5, 1);
	cairo_line_to(cr, x+ds*0.5, 1);

	cairo_move_to(cr, x-ds*0.5, v->line_height - 1);
	cairo_line_to(cr, x+ds*0.5, v->line_height - 1);

	cairo_set_line_width(cr, 1);
	cairo_stroke(cr);

	cairo_restore(cr);
}

void
draw_line(cairo_t *cr, view_t *v, size_t nr)
{
	glyphs_t *gl = view_get_glyphs(v, nr);

	cairo_save(cr);

	double yoff = v->line_height * (nr - v->start);
	cairo_translate(cr, 0, yoff);

	double sel_s = 0;
	double sel_e = 0;

	if(nr == v->range.start.line) {
		sel_s = view_address_to_x(v, &v->range.start);
	}
	if(nr == v->range.end.line) {
		sel_e = view_address_to_x(v, &v->range.end);
	} else if(nr >= v->range.start.line && nr < v->range.end.line) {
		sel_e = v->width;
	}

	cairo_save(cr);
	cairo_set_source_rgb(cr, 0.625, 0.75, 1);
	cairo_rectangle(cr, sel_s, 0, sel_e - sel_s, v->line_height);
	cairo_clip(cr);
	cairo_paint(cr);
	cairo_restore(cr);

	if(nr == v->range.start.line && !address_cmp(&v->range.start, &v->range.end)) {
		draw_cursor(cr, v, &v->range.start);
	}

	cairo_translate(cr, v->left_margin, v->extents.ascent);
	cairo_show_glyphs(cr, gl->data, gl->nmemb);

	cairo_restore(cr);
}

void
draw_button(cairo_t *cr, view_t *v, button_t *btn)
{
	cairo_translate(cr, v->left_margin, 0);

	cairo_save(cr);
	cairo_translate(cr, 0, v->extents.ascent);
	cairo_show_glyphs(cr, btn->glyphs.data, btn->glyphs.nmemb);
	cairo_restore(cr);

	cairo_translate(cr, btn->glyphs.data[btn->glyphs.nmemb - 1].x +
			v->left_margin, 0);

	cairo_move_to(cr, 0, 0);
	cairo_line_to(cr, 0, v->line_height);
	cairo_set_line_width(cr, 1);
	cairo_stroke(cr);
}

void
draw_toolbar(cairo_t *cr, view_t *v, toolbar_wrap_t *bar_wrap)
{
	cairo_save(cr);

	double yoff = v->line_height * (bar_wrap->line - v->start);
	cairo_translate(cr, 0, yoff);

	cairo_save(cr);
	cairo_set_source_rgb(cr, 0.875, 0.75, 0.5);
	// salmon //cairo_set_source_rgb(cr, 1, 0.5, 0.5);
	// yellow green // cairo_set_source_rgb(cr, 0.625, 0.75, 0.25);
	cairo_rectangle(cr, 0, 0, v->width, v->line_height);
	cairo_clip(cr);
	cairo_paint(cr);

	cairo_move_to(cr, 0, 0);
	cairo_line_to(cr, v->width, 0);
	cairo_move_to(cr, 0, v->line_height);
	cairo_line_to(cr, v->width, v->line_height);
	cairo_set_line_width(cr, 1);
	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_stroke(cr);

	cairo_restore(cr);

	double s = 0.625;
	cairo_matrix_t mat;
	cairo_get_font_matrix(cr, &mat);
	cairo_set_font_size(cr, mat.xx * s);

	for(size_t i = 0; i < bar_wrap->bar.buttons.nmemb; i++) {
		draw_button(cr, v, &bar_wrap->bar.buttons.data[i]);
	}

	cairo_restore(cr);
	cairo_translate(cr, 0, v->line_height);
}

void
draw_view(cairo_t *cr, view_t *v)
{
	if(!v->nmemb) {
		return;
	}
	size_t start = clampss(v->start, 0, v->range.file->content.nmemb - 1);
	size_t end = view_clamp_start(v, v->start + v->nmemb - 1);
	size_t i = start;
	for(; i <= end; i++) {
		if(v->selbar_wrap.visible && i == v->selbar_wrap.line) {
			draw_toolbar(cr, v, &v->selbar_wrap);
		}
		draw_line(cr, v, i);
	}
	if(v->selbar_wrap.visible && i == v->selbar_wrap.line) {
		draw_toolbar(cr, v, &v->selbar_wrap);
	}
}
