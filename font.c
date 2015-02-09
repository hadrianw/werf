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

static fontidx_t
fontset_match_codepoint(fontset_t *f, FcChar32 codepoint)
{
	FcCharSet *chset;
	int maxlen = MIN(FONTIDX_MAX, f->set->nfont);

	for(int i = 0; i < maxlen; i++) {
		if( !FcPatternGetCharSet(f->set->fonts[i], FC_CHARSET, 0, &chset) &&
			FcCharSetHasChar(chset, codepoint)) {
			return i;
		}
	}
	return FONTIDX_MAX;
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

/*
0-1-2
|X|X|
3-4-5
|X|X|
6-7-8
*/

static struct { double x, y; } idx_to_coords[] = {
	[0] = {.0, -1.},
	[1] = {.5, -1.},
	[2] = {1., -1.},
	[3] = {.0, -.5},
	[4] = {.5, -.5},
	[5] = {1., -.5},
	[6] = {.0, -.0},
	[7] = {.5, -.0},
	[8] = {1., -.0},
};

enum { SEP = LEN(idx_to_coords), STP };

static uchar ch0[] = {0, 2, 8, 6, 0, SEP, 2, 6, STP};
static uchar ch1[] = {4, 2, 8, STP};
static uchar ch2[] = {0, 2, 5, 3, 6, 8, STP};
static uchar ch3[] = {0, 2, 8, 6, SEP, 3, 5, STP};
static uchar ch4[] = {0, 3, 5, SEP, 2, 8, STP};
static uchar ch5[] = {2, 0, 3, 5, 8, 6, STP};
static uchar ch6[] = {2, 0, 6, 8, 5, 3, STP};
static uchar ch7[] = {0, 2, 4, 7, STP};
static uchar ch8[] = {0, 2, 8, 6, 0, SEP, 3, 5, STP};
static uchar ch9[] = {6, 8, 2, 0, 3, 5, STP};
static uchar chA[] = {6, 3, 1, 5, 8, SEP, 3, 5, STP};
static uchar chB[] = {0, 2, 8, 6, SEP, 1, 7, SEP, 4, 5, STP};
static uchar chC[] = {2, 0, 6, 8, STP};
static uchar chD[] = {0, 2, 8, 6, SEP, 1, 7, STP};
static uchar chE[] = {2, 0, 6, 8, SEP, 3, 5, STP};
static uchar chF[] = {2, 0, 6, SEP, 3, 5, STP};
static uchar chG[] = {2, 0, 6, 8, 5, 4, STP};
static uchar chH[] = {0, 6, SEP, 2, 8, SEP, 3, 5, STP};
static uchar chI[] = {0, 2, SEP, 6, 8, SEP, 1, 7, STP};
static uchar chJ[] = {2, 8, 6, 3, STP};
static uchar chK[] = {0, 6, SEP, 2, 3, 8, STP};
static uchar chL[] = {0, 6, 8, STP};
static uchar chM[] = {6, 0, 4, 2, 8, STP};
static uchar chN[] = {6, 0, 8, 2, STP};
static uchar chO[] = {0, 2, 8, 6, 0, STP};
static uchar chP[] = {6, 0, 2, 5, 3, STP};
static uchar chQ[] = {8, 6, 0, 2, 8, 4, STP};
static uchar chR[] = {6, 0, 2, 5, 3, 8, STP};
static uchar chS[] = {2, 1, 3, 5, 8, 6, STP};
static uchar chT[] = {0, 2, SEP, 1, 7, STP};
static uchar chU[] = {0, 6, 8, 2, STP};
static uchar chV[] = {0, 7, 2, STP};
static uchar chW[] = {0, 6, 4, 8, 2, STP};
static uchar chX[] = {0, 8, SEP, 2, 6, STP};
static uchar chY[] = {0, 4, 7, SEP, 2, 4, STP};
static uchar chZ[] = {0, 2, 6, 8, STP};

uchar *ch_to_idx[] = {
	['0' - 0x30] = ch0,
	['1' - 0x30] = ch1,
	['2' - 0x30] = ch2,
	['3' - 0x30] = ch3,
	['4' - 0x30] = ch4,
	['5' - 0x30] = ch5,
	['6' - 0x30] = ch6,
	['7' - 0x30] = ch7,
	['8' - 0x30] = ch8,
	['9' - 0x30] = ch9,
	['A' - 0x30] = chA,
	['B' - 0x30] = chB,
	['C' - 0x30] = chC,
	['D' - 0x30] = chD,
	['E' - 0x30] = chE,
	['F' - 0x30] = chF,
	['G' - 0x30] = chG,
	['H' - 0x30] = chH,
	['I' - 0x30] = chI,
	['J' - 0x30] = chJ,
	['K' - 0x30] = chK,
	['L' - 0x30] = chL,
	['M' - 0x30] = chM,
	['N' - 0x30] = chN,
	['O' - 0x30] = chO,
	['P' - 0x30] = chP,
	['Q' - 0x30] = chQ,
	['R' - 0x30] = chR,
	['S' - 0x30] = chS,
	['T' - 0x30] = chT,
	['U' - 0x30] = chU,
	['V' - 0x30] = chV,
	['W' - 0x30] = chW,
	['X' - 0x30] = chX,
	['Y' - 0x30] = chY,
	['Z' - 0x30] = chZ
};

static struct {
	uchar id[3];
} ctrl_to_id[] = {
	[0x00] = {{'N', 'U', 'L'}},
	[0x01] = {{'S', 'O', 'H'}},
	[0x02] = {{'S', 'T', 'X'}},
	[0x03] = {{'E', 'T', 'X'}},
	[0x04] = {{'E', 'O', 'T'}},
	[0x05] = {{'E', 'N', 'Q'}},
	[0x06] = {{'A', 'C', 'K'}},
	[0x07] = {{'B', 'E', 'L'}},
	[0x08] = {{'B', 'S'}},
	[0x09] = {{'H', 'T'}},
	[0x0A] = {{'L', 'F'}},
	[0x0B] = {{'V', 'T'}},
	[0x0C] = {{'F', 'F'}},
	[0x0D] = {{'C', 'R'}},
	[0x0E] = {{'S', 'O'}},
	[0x0F] = {{'S', 'I'}},
	[0x10] = {{'D', 'L', 'E'}},
	[0x11] = {{'D', 'C', '1'}},
	[0x12] = {{'D', 'C', '2'}},
	[0x13] = {{'D', 'C', '3'}},
	[0x14] = {{'D', 'C', '4'}},
	[0x15] = {{'N', 'A', 'K'}},
	[0x16] = {{'S', 'Y', 'N'}},
	[0x17] = {{'E', 'T', 'B'}},
	[0x18] = {{'C', 'A', 'N'}},
	[0x19] = {{'E', 'M'}},
	[0x1A] = {{'S', 'U', 'B'}},
	[0x1B] = {{'E', 'S', 'C'}},
	[0x1C] = {{'F', 'S'}},
	[0x1D] = {{'G', 'S'}},
	[0x1E] = {{'R', 'S'}},
	[0x1F] = {{'U', 'S'}},
};

#define SPECIAL_WIDTH 1.5

void
basic_text(cairo_t *cr, const uchar *str, size_t len, double x, double adv, double chw, double chh)
{
	for(size_t i = 0; i < len; i++) {
		uchar *chidx = ch_to_idx[str[i] - 0x30];
		DIEIF(chidx == NULL);
		void (*cr_line)(cairo_t*, double, double) = cairo_move_to;
		for(uchar j = 0; chidx[j] != STP; j++) {
			if(chidx[j] == SEP) {
				cr_line = cairo_move_to;
				continue;
			}

			double crdx = idx_to_coords[chidx[j]].x * chw;
			double crdy = idx_to_coords[chidx[j]].y * chh;
			cr_line(cr, x + crdx, crdy);
			cr_line = cairo_line_to;
		}
		x += adv;
	}
}

static cairo_status_t
dt_face_render_glyph(cairo_scaled_font_t *scaled_font,
		unsigned long glyph, cairo_t *cr,
		cairo_text_extents_t *extents)
{
	fontset_t *fontset;

	if(get_fontidx(glyph) == FONTIDX_MAX) {
		memset(extents, 0, sizeof extents[0]);
		uint32_t codepoint = get_glyphidx(glyph);
		if(codepoint == '\n' || codepoint == '\t') {
			return CAIRO_STATUS_SUCCESS;
		}
		uchar *id;
		uchar len = 0;
		enum { BYTE_LEN = 2, CODEPOINT_LEN = 6 };
		uchar buf[MAX(BYTE_LEN, CODEPOINT_LEN) + 1];
		if(codepoint < LEN(ctrl_to_id)) {
			id = ctrl_to_id[codepoint].id;
			while(id[len] && len < LEN(ctrl_to_id[0].id)) {
				len++;
			}
		} else if(codepoint < 256) {
			id = buf;
			len = BYTE_LEN;
			sprintf((char*)id, "%0*X", (int)len, codepoint);
		} else if(codepoint & CODEPOINT_NOT_FOUND) {
			codepoint ^= CODEPOINT_NOT_FOUND;
			id = buf;
			len = snprintf((char*)id, CODEPOINT_LEN+1, "%X", codepoint);
		} else {
			return CAIRO_STATUS_USER_FONT_ERROR;
		}

		double adv = SPECIAL_WIDTH / len * 0.875;
		double chw = adv * 0.6875;
		double chh = 0.6875;

		double x = ( SPECIAL_WIDTH - (len*adv - (adv-chw)) ) * 0.5;

		basic_text(cr, id, len, x, adv, chw, chh);

		cairo_set_line_join(cr, CAIRO_LINE_JOIN_BEVEL);
		cairo_matrix_t mat;
		cairo_scaled_font_get_font_matrix(scaled_font, &mat);
		cairo_set_line_width(cr, 1 / mat.xx);
		cairo_stroke(cr);
		return CAIRO_STATUS_SUCCESS;
	}

	fontset = cairo_font_face_get_user_data(
			cairo_scaled_font_get_font_face(scaled_font), &dt_face_key);

	cairo_set_scaled_font(cr, fontset_get_font(fontset, get_fontidx(glyph)));

	cairo_glyph_t cr_glyph = {.index = get_glyphidx(glyph)};
	cairo_show_glyphs(cr, &cr_glyph, 1);
	cairo_glyph_extents(cr, &cr_glyph, 1, extents);

	return CAIRO_STATUS_SUCCESS;
}

static uint32_t
dt_get_char_index(FT_Face face, FT_ULong charcode)
{
	FT_UInt idx = FT_Get_Char_Index(face, charcode);
	if(idx > GLYPHIDX_MAX) {
		printf("%lu -> %u (> %u)\n", charcode, idx, GLYPHIDX_MAX);
		return 0;
	}
	return idx;
}

static double
dt_get_kerning(FT_Face face, unsigned long left, unsigned long right)
{
	if(face == NULL || get_fontidx(left) == FONTIDX_MAX ||
			get_fontidx(right) == FONTIDX_MAX ||
			get_glyphidx(left) == 0 || get_glyphidx(right) == 0 ||
			get_fontidx(left) != get_fontidx(right)) {
		return 0;
	}

	FT_Vector k;
	if(FT_Get_Kerning(face, get_glyphidx(left), get_glyphidx(right),
			FT_KERNING_UNFITTED, &k)) {
		return 0;
	}

	return (double)k.x / (1<<6);
}

static double
dt_get_advance(FT_Face face, unsigned long glyph)
{
	if(face == NULL || get_fontidx(glyph) == FONTIDX_MAX) {
		//if(get_glyphidx(glyph) == '\n' || get_glyphidx(glyph) == '\t') {
		if(get_glyphidx(glyph) == '\n') {
			return 0;
		}
		return SPECIAL_WIDTH;
	}
	FT_Fixed v;
	FT_Int32 load_flags = FT_LOAD_DEFAULT | FT_LOAD_NO_HINTING;
	if(FT_Get_Advance(face, get_glyphidx(glyph), load_flags, &v)) {
		return 0;
	}
	return (double)v / (1<<16);
}

static size_t
utf8glyph(fontset_t *fset, const char *utf8, size_t utf8_len, FT_Face *face, unsigned long *glyph)
{
	long codepoint;
	size_t chsiz = utf8decode(utf8, &codepoint, utf8_len);
	if( codepoint == '\t' || codepoint == '\n' || codepoint == UTF_INVALID ||
			(chsiz == 1 && !isprint(utf8[0])) ) {
		*glyph = make_cr_glyph(FONTIDX_MAX, (uchar)utf8[0]);
		*face = NULL;
		return 1;
	}

	fontidx_t fontidx = fontset_match_codepoint(fset, codepoint);
	if(fontidx == FONTIDX_MAX) {
		*glyph = make_cr_glyph(FONTIDX_MAX, codepoint | CODEPOINT_NOT_FOUND);
		*face = NULL;
		return chsiz;
	}

	*face = cairo_ft_scaled_font_lock_face(fontset_get_font(fset, fontidx));
	uint32_t glyphidx = dt_get_char_index(*face, codepoint);
	if(glyphidx == 0) {
		printf("0: %lu @ %u\n", codepoint, fontidx);
	}
	*glyph = make_cr_glyph(fontidx, glyphidx);
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

	cairo_glyph_t glyph = {0};
	unsigned long prev_glyph_idx;

	FT_Face face;

	for(int off = 0; off < utf8_len; off += chsiz, i++) {
		if(i == max_num_glyphs) {
			max_num_glyphs = MAX(max_num_glyphs * 3 / 2, utf8_len);
			aglyphs = reallocdup(aglyphs, max_num_glyphs, glyphs, i,
				sizeof glyphs[0]);
			glyphs = aglyphs;
		}

		prev_glyph_idx = glyph.index;
		chsiz = utf8glyph(fset, utf8+off, utf8_len-off, &face, &glyph.index);

		glyph.x += dt_get_kerning(face, prev_glyph_idx, glyph.index);
		glyphs[i] = glyph;
		glyph.x += dt_get_advance(face, glyph.index);

		if(face != NULL) {
			cairo_ft_scaled_font_unlock_face( fontset_get_font(
					fset, get_fontidx(glyph.index)) );
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
