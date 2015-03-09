#include <stdint.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <fontconfig/fontconfig.h>

#include <cairo/cairo.h>

typedef struct {
	FcPattern *pattern;
	FcFontSet *set;
	cairo_scaled_font_t **cache;
	bool onheap;
} fontset_t;

int fontset_load_font(fontset_t *f, unsigned int index);
int fontset_init(fontset_t *f, FcPattern *pattern);
void fontset_free(fontset_t *f);

cairo_font_face_t *font_cairo_font_face_create(fontset_t *fontset);

cairo_status_t font_text_to_glyphs(cairo_scaled_font_t *scaled_font,
		const char *utf8, int utf8_len,
		cairo_glyph_t **out_glyphs, int *out_num_glyphs,
		cairo_text_cluster_t **out_clusters, int *out_num_clusters,
		cairo_text_cluster_flags_t *cluster_flags);

typedef uint8_t fontidx_t;
enum {
	FONTIDX_MAX = (1 << sizeof(fontidx_t) * 8) - 1,
	GLYPHIDX_MAX = UINT32_MAX >> sizeof(fontidx_t) * 8,
	CODEPOINT_MAX = GLYPHIDX_MAX >> 1,
	CODEPOINT_NOT_FOUND = 1 << ((sizeof(uint32_t) - sizeof(fontidx_t)) * 8 - 1)
};

static inline unsigned long make_cr_glyph(fontidx_t fontidx, uint32_t glyphidx)
{
	return fontidx | (glyphidx << (sizeof(fontidx) * 8));
}

static inline fontidx_t get_fontidx(unsigned long cr_glyph)
{
	return cr_glyph & FONTIDX_MAX;
}

static inline uint32_t get_glyphidx(unsigned long cr_glyph)
{
	return cr_glyph >> (sizeof(fontidx_t) * 8);
}
