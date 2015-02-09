#include <ctype.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "util.h"

#include "font.h"
#include FT_ADVANCES_H
#include FT_GLYPH_H
#include FT_OUTLINE_H
#include <cairo/cairo-ft.h>

#include "utf.h"

static cairo_scaled_font_t *
fontset_get_font(fontset_t *f, unsigned int index)
{
	cairo_font_face_t *face;
	cairo_matrix_t mat;

	if(f->cache[index] != NULL) {
		return f->cache[index];
	}

	face = cairo_ft_font_face_create_for_pattern(f->set->fonts[index]);
	if(!face) {
		fputs("cairo_ft_font_face_create_for_pattern failed\n", stderr);
		return NULL;
	}

	cairo_matrix_init_identity(&mat);
	cairo_font_options_t *opts = cairo_font_options_create();
	f->cache[index] = cairo_scaled_font_create(face, &mat, &mat, opts);
	cairo_font_options_destroy(opts);
	cairo_font_face_destroy(face);

	if(f->cache[index] == NULL) {
		fputs("cairo_scaled_font_create failed\n", stderr);
		return NULL;
	}
	return f->cache[index];
}

int
fontset_init(fontset_t *f, FcPattern *pattern)
{
	FcResult res;

	FcConfigSubstitute(NULL, pattern, FcMatchPattern);
	FcDefaultSubstitute(pattern);
	FcPatternAddBool(pattern, FC_SCALABLE, FcTrue);

	f->set = FcFontSort(NULL, pattern, FcTrue, NULL, &res);
	if(f->set == NULL) {
		fputs("FcFontSort failed\n", stderr);
		return -1;
	}

	f->cache = xcalloc(f->set->nfont, sizeof f->cache[0]);

	f->pattern = pattern;
	fontset_get_font(f, 0);
	return 0;
}

static uint16_t
fontset_match_codepoint(fontset_t *f, FcChar32 codepoint)
{
	FcCharSet *chset;
	int maxlen = MIN(UINT16_MAX, f->set->nfont);

	for(int i = 0; i < maxlen; i++) {
		if( !FcPatternGetCharSet(f->set->fonts[i], FC_CHARSET, 0, &chset) &&
			FcCharSetHasChar(chset, codepoint)) {
			return i;
		}
	}
	return UINT16_MAX;
}

void
fontset_free(fontset_t *f)
{
	if(f == NULL) {
		return;
	}
	for(int i = 0; i < f->set->nfont; i++) {
		if(f->cache[i]) {
			cairo_scaled_font_destroy(f->cache[i]);
		}
	}
	free(f->cache);
	FcFontSetDestroy(f->set);
	FcPatternDestroy(f->pattern);
	if(f->onheap) {
		free(f);
	}
}

/* cairo user font */

static cairo_user_data_key_t dt_face_key;

static cairo_status_t
dt_face_init(cairo_scaled_font_t *scaled_font,
		cairo_t *cr, cairo_font_extents_t *out_extents)
{
	fontset_t *fontset = cairo_font_face_get_user_data(
			cairo_scaled_font_get_font_face(scaled_font), &dt_face_key);

	cairo_set_scaled_font(cr, fontset->cache[0]);
	cairo_font_extents(cr, out_extents);
	return CAIRO_STATUS_SUCCESS;
}

static cairo_status_t
dt_face_render_glyph(cairo_scaled_font_t *scaled_font,
		unsigned long glyph, cairo_t *cr,
		cairo_text_extents_t *extents)
{
	fontset_t *fontset;
	glyphidx_t dt_glyph = {.index = glyph};

	if(dt_glyph.props.font_idx == UINT16_MAX) {
		memset(extents, 0, sizeof extents[0]);
		return CAIRO_STATUS_SUCCESS;
	}

	fontset = cairo_font_face_get_user_data(
			cairo_scaled_font_get_font_face(scaled_font), &dt_face_key);

	cairo_set_scaled_font(cr, fontset_get_font(fontset, dt_glyph.props.font_idx));

	cairo_glyph_t cr_glyph = {dt_glyph.props.glyph_idx};
	cairo_show_glyphs(cr, &cr_glyph, 1);
	cairo_glyph_extents(cr, &cr_glyph, 1, extents);

	return CAIRO_STATUS_SUCCESS;
}

static uint16_t
dt_get_char_index(FT_Face face, FT_ULong charcode)
{
	FT_UInt idx = FT_Get_Char_Index(face, charcode);
	if(idx > UINT16_MAX) {
		return 0;
	}
	return idx;
}

static double
dt_get_kerning(FT_Face face, glyphidx_t left, glyphidx_t right)
{
	if(face == NULL || left.props.font_idx == UINT16_MAX ||
			right.props.font_idx == UINT16_MAX ||
			left.props.glyph_idx == 0 || right.props.glyph_idx == 0 ||
			left.props.font_idx != right.props.font_idx) {
		return 0;
	}

	FT_Vector k;
	if(FT_Get_Kerning(face, left.props.glyph_idx, right.props.glyph_idx,
			FT_KERNING_UNFITTED, &k)) {
		return 0;
	}

	return (double)k.x / (1<<6);
}

static double
dt_get_advance(FT_Face face, glyphidx_t glyph, double def)
{
	if(face == NULL || glyph.props.font_idx == UINT16_MAX) {
		if(glyph.props.glyph_idx == '\n' || glyph.props.glyph_idx == '\t') {
			return 0;
		}
		return def;
	}
	FT_Fixed v;
	FT_Int32 load_flags = FT_LOAD_DEFAULT | FT_LOAD_NO_HINTING;
	if(FT_Get_Advance(face, glyph.props.glyph_idx, load_flags, &v)) {
		return 0;
	}
	return (double)v / (1<<16);
}

static size_t
utf8glyph(fontset_t *fset, const char *utf8, size_t utf8_len, FT_Face *face, glyphidx_t *glyph)
{
	long codepoint;
	size_t chsiz = utf8decode(utf8, &codepoint, utf8_len);
	if( codepoint == '\t' || codepoint == '\n' || codepoint == UTF_INVALID ||
			(chsiz == 1 && !isprint(codepoint)) ) {
		glyph->props.font_idx = UINT16_MAX;
		glyph->props.glyph_idx = utf8[0];
		*face = NULL;
		return 1;
	}

	glyph->props.font_idx = fontset_match_codepoint(fset, codepoint);
	if(glyph->props.font_idx == UINT16_MAX) {
		glyph->props.font_idx = 0;
	}

	*face = cairo_ft_scaled_font_lock_face(fontset_get_font(fset, glyph->props.font_idx));
	glyph->props.glyph_idx = dt_get_char_index(*face, codepoint);
	return chsiz;
}

static cairo_status_t
dt_face_text_to_glyphs(cairo_scaled_font_t *scaled_font,
		const char *utf8, int utf8_len,
		cairo_glyph_t **out_glyphs, int *out_num_glyphs,
		cairo_text_cluster_t **out_clusters, int *out_num_clusters,
		cairo_text_cluster_flags_t *cluster_flags)
{
	fontset_t *fset = cairo_font_face_get_user_data(
			cairo_scaled_font_get_font_face(scaled_font), &dt_face_key);

	size_t chsiz;
	int i = 0;
	cairo_glyph_t *aglyphs = NULL;
	cairo_glyph_t *glyphs = *out_glyphs;
	int max_num_glyphs = *out_num_glyphs;

	cairo_glyph_t cr_glyph = {0};
	glyphidx_t glyph = {0};
	glyphidx_t prev_glyph;

	FT_Face face;

	for(int off = 0; off < utf8_len; off += chsiz, i++) {
		if(i == max_num_glyphs) {
			max_num_glyphs = MAX(max_num_glyphs * 3 / 2, utf8_len);
			aglyphs = reallocdup(aglyphs, max_num_glyphs, glyphs, i,
				sizeof glyphs[0]);
			glyphs = aglyphs;
		}

		prev_glyph = glyph;
		chsiz = utf8glyph(fset, utf8+off, utf8_len-off, &face, &glyph);

		cr_glyph.x += dt_get_kerning(face, prev_glyph, glyph);
		cr_glyph.index = glyph.index;
		glyphs[i] = cr_glyph;
		cr_glyph.x += dt_get_advance(face, glyph, 0.5);

		if(face != NULL) {
			cairo_ft_scaled_font_unlock_face(fontset_get_font(fset, glyph.props.font_idx));
		}
	}

	*out_glyphs = glyphs;
	*out_num_glyphs = i;

	(void)out_clusters;
	(void)out_num_clusters;
	(void)cluster_flags;

	return CAIRO_STATUS_SUCCESS;
}

cairo_font_face_t *
cairo_dt_face_create(fontset_t *fontset)
{
	cairo_font_face_t *uface;
	cairo_status_t status;

	uface = cairo_user_font_face_create();
	cairo_user_font_face_set_init_func(uface, dt_face_init);
	cairo_user_font_face_set_render_glyph_func(uface, dt_face_render_glyph);
	cairo_user_font_face_set_text_to_glyphs_func(uface, dt_face_text_to_glyphs);

	status = cairo_font_face_set_user_data(uface, &dt_face_key, fontset,
			(cairo_destroy_func_t)fontset_free);
	if(status) {
		fontset_free(fontset);
		return NULL;
	}

	return uface;
}
