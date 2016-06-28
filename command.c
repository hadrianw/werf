#include <X11/Xlib.h>

#include <cairo/cairo.h>

#include "util.h"
#include "array.h"

#include "edit.h"
#include "view.h"

void
command_undo(view_t *v)
{
	file_undo(&v->range);
	v->last_x = view_address_to_x(v, &v->range.start);
}

void
command_redo(view_t *v)
{
	file_redo(&v->range);
	v->last_x = view_address_to_x(v, &v->range.start);
}

void
command_page_up(view_t *v)
{
	view_move_start(v, -v->nmemb);
}

void
command_page_down(view_t *v)
{
	view_move_start(v, v->nmemb);
}

void
command_home(view_t *v)
{
	v->range.start.offset = 0;
	v->range.end.offset = v->range.start.offset;
	v->last_x = view_address_to_x(v, &v->range.start);
}

void
command_end(view_t *v)
{
	glyphs_t *gl = view_get_glyphs(v, v->range.start.line);
	v->range.start.offset = gl->glyph_to_offset[gl->nmemb - 1];
	v->range.end.offset = v->range.start.offset;
	v->last_x = view_address_to_x(v, &v->range.start);
}

void
command_backspace(view_t *v)
{
	optype_t type = OP_Replace;
	if(!address_cmp(&v->range.start, &v->range.end)) {
		view_move_address(v, &v->range.start, -1);
		type = OP_BackSpace;
	}
	range_push(&v->range, "", 0, type);
	v->last_x = view_address_to_x(v, &v->range.start);
}

void
command_delete(view_t *v)
{
	optype_t type = OP_Replace;
	if(!address_cmp(&v->range.start, &v->range.end)) {
		view_move_address(v, &v->range.end, 1);
		type = OP_Delete;
	}
	range_push(&v->range, "", 0, type);
	v->last_x = view_address_to_x(v, &v->range.start);
}

void
command_new_line(view_t *v)
{
	range_push(&v->range, "\n", 1, OP_Char);
	v->last_x = view_address_to_x(v, &v->range.start);
}

void
command_left(view_t *v)
{
	if(!address_cmp(&v->range.start, &v->range.end)) {
		view_move_address(v, &v->range.start, -1);
	}
	v->range.end = v->range.start;
	v->last_x = view_address_to_x(v, &v->range.start);
}

void
command_right(view_t *v)
{
	if(!address_cmp(&v->range.start, &v->range.end)) {
		view_move_address(v, &v->range.start, 1);
	}
	v->range.end = v->range.start;
	v->last_x = view_address_to_x(v, &v->range.start);
}

void
command_up(view_t *v)
{
	if(!address_cmp(&v->range.start, &v->range.end)) {
		view_move_address_line(v, &v->range.start, -1);
	}
	v->range.end = v->range.start;
}

void
command_down(view_t *v)
{
	if(!address_cmp(&v->range.start, &v->range.end)) {
		view_move_address_line(v, &v->range.start, 1);
	}
	v->range.end = v->range.start;
}
