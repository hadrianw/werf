typedef struct {
	cairo_glyph_t *data;
	int nmemb;
	size_t *glyph_to_offset;
	int *offset_to_glyph;
} glyphs_t;

typedef struct {
	string_t label;
	glyphs_t glyphs;
} button_t;

typedef struct {
	ARRAY(button_t) buttons;
} toolbar_t;

typedef struct {
	size_t line;
	bool visible;
	toolbar_t bar;
} toolbar_wrap_t;

typedef struct {
	int width;
	int height;
	cairo_font_extents_t extents;
	double line_height;
	double left_margin;
	cairo_scaled_font_t *font;

	range_t range;
	int last_x;
	ssize_t start;
	glyphs_t *lines;
	size_t nmemb;
	toolbar_wrap_t selbar_wrap;
} view_t;

void glyphs_from_text(glyphs_t *gl, cairo_scaled_font_t *font, string_t *line);

bool toolbar_click(toolbar_t *bar, view_t *v, int x);

void view_hide_toolbar(view_t *v, toolbar_wrap_t *bar_wrap, size_t line);

bool view_keybinds(view_t *v, KeySym keysym);
bool view_keypress(view_t *v, KeySym keysym, char *buf, size_t len);
bool view_mouse_motion(view_t *v, unsigned int state, int x, int y, int relx, int rely);
bool view_mouse_press(view_t *v, unsigned int btn, int x, int y);
bool view_mouse_release(view_t *v, unsigned int btn, int x, int y);

void view_move_address_line(view_t *v, address_t *adr, int move);
void view_move_address(view_t *v, address_t *adr, int move);

bool view_move_start(view_t *v, ssize_t move);
void view_set_start(view_t *v, size_t nr);

glyphs_t *view_get_glyphs(view_t *v, size_t nr);
ssize_t view_clamp_start(view_t *v, ssize_t nr);
double view_address_to_x(view_t *v, address_t *adr);
size_t view_x_to_offset(view_t *v, size_t nr, int x);
void view_xy_to_address(view_t *v, int x, int y, address_t *adr);
ssize_t view_y_to_line(view_t *v, int y);

void view_resize(view_t *v, int width, int height);
void view_reshape(view_t *v);
