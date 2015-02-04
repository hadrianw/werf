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

cairo_font_face_t *cairo_dt_face_create(fontset_t *fontset);

typedef union {
	unsigned long index;
	struct {
		uint16_t font_idx;
		uint16_t glyph_idx;
	} props;
} glyphidx_t;
