typedef struct {
	int x;
	int y;
	view_t view;
} view_wrap_t;

typedef struct {
	int width;
	int height;
	Display *display;
	int screen;
	GC gfxctx;
	Window window;
	Drawable pixmap;
	cairo_t *cr;
	XIM xim;
	XIC xic;
	view_wrap_t *view_wrap;
	bool run;

	int prevx;
	int prevy;
} window_t;

void window_init(window_t *win);
void window_deinit(window_t *win);
void window_run(window_t *win);
void window_redraw(window_t *win);
