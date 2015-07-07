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

#include "edit.h"
#include "view.h"
#include "draw.h"
#include "window.h"

void
window_redraw(window_t *win)
{
	XFillRectangle(win->display, win->pixmap, win->gfxctx,
			0, 0, win->width, win->height);

	cairo_identity_matrix(win->cr);
	cairo_set_source_rgba(win->cr, 0, 0, 0, 1);

	draw_view(win->cr, &win->view_wrap->view);
	
	XCopyArea(win->display, win->pixmap, win->window, win->gfxctx,
			0, 0, win->width, win->height, 0, 0);
}

static bool
window_resize(window_t *win, XEvent *ev)
{
	if(win->width == ev->xconfigure.width && win->height == ev->xconfigure.height) {
		return false;
	}
	win->width = ev->xconfigure.width;
	win->height = ev->xconfigure.height;

	XFreePixmap(win->display, win->pixmap);
	win->pixmap = XCreatePixmap(win->display, win->window, win->width, win->height,
			DefaultDepth(win->display, win->screen));

	cairo_xlib_surface_set_drawable(cairo_get_target(win->cr),
			win->pixmap, win->width, win->height);

	cairo_font_extents(win->cr, &win->view_wrap->view.extents);
	if(!win->view_wrap->view.font) {
		win->view_wrap->view.font = cairo_get_scaled_font(win->cr);
	}
	view_resize(&win->view_wrap->view, win->width, win->height);
	return true;
}

static void
window_init_input_methods(window_t *win)
{
	char *locale_mods[] = {
		"", 
		"@im=local",
		"@im=",
	};
	size_t i = 0;

	do {
		XSetLocaleModifiers(locale_mods[i++]);
		win->xim = XOpenIM(win->display, NULL, NULL, NULL);
	} while(win->xim == NULL && i < LEN(locale_mods));

	DIEIF(win->xim == NULL);

	win->xic = XCreateIC(win->xim, XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
			XNClientWindow, win->window, XNFocusWindow, win->window, NULL);
	DIEIF(win->xic == NULL);
}

void
window_init(window_t *win)
{
	win->display = XOpenDisplay(NULL);
	DIEIF(win->display == NULL);

	Window parent = XRootWindow(win->display, win->screen);
	win->screen = XDefaultScreen(win->display);
	win->window = XCreateSimpleWindow(win->display, parent,
			0, 0, win->width, win->height, 0, 0,
			WhitePixel(win->display, win->screen));

	win->gfxctx = XCreateGC(win->display, parent, GCGraphicsExposures,
			&(XGCValues){.graphics_exposures = False});
	XSetForeground(win->display, win->gfxctx, WhitePixel(win->display, win->screen));

	window_init_input_methods(win);

	XSelectInput(win->display, win->window, StructureNotifyMask | ExposureMask |
		KeyPressMask | ButtonMotionMask | ButtonPressMask | ButtonReleaseMask);
	XMapWindow(win->display, win->window);

	XSetWMProperties(win->display, win->window, NULL, NULL, NULL, 0, NULL,
			&(XWMHints){.flags = InputHint, .input = 1},
			&(XClassHint){"Werf", "Werf"});
	XSync(win->display, False);

	for(XEvent ev;;) {
		XNextEvent(win->display, &ev);
		if(XFilterEvent(&ev, None))
			continue;
		if(ev.type == ConfigureNotify) {
			win->width = ev.xconfigure.width;
			win->height = ev.xconfigure.height;
		} else if(ev.type == MapNotify) {
			break;
		}
	}

	win->pixmap = XCreatePixmap(win->display, win->window, win->width, win->height,
			DefaultDepth(win->display, win->screen));
	cairo_surface_t *bufsurf = cairo_xlib_surface_create(win->display, win->pixmap, 
			DefaultVisual(win->display, win->screen), win->width, win->height);
	win->cr = cairo_create(bufsurf);

	win->run = true;
}

void
window_deinit(window_t *win)
{
	cairo_destroy(win->cr);

	XCloseDisplay(win->display);
	win->display = NULL;
}

static bool
window_keypress(window_t *win, XEvent *ev)
{
	KeySym keysym;
	char buf[32];
	Status status;
	XKeyEvent *e = &ev->xkey;
	int len = Xutf8LookupString(win->xic, e, buf, sizeof buf, &keysym, &status);

	if(keysym == XK_Escape) {
		win->run = false;
		return true;
	}

	return view_keypress(&win->view_wrap->view, keysym, buf, len > 0 ? len : 0);
}

static bool
window_mouse_press(window_t *win, XEvent *ev)
{
	bool handled;
	XButtonEvent *e = &ev->xbutton;
	int x = e->x - win->view_wrap->x;
	int y = e->y - win->view_wrap->y;

	handled = view_mouse_press(&win->view_wrap->view, e->button, x, y);

	win->prevx = e->x;
	win->prevy = e->y;
	return handled;
}

static bool
window_mouse_motion(window_t *win, XEvent *ev)
{
	bool handled;
	XMotionEvent *e = &ev->xmotion;
	int x = e->x - win->view_wrap->x;
	int y = e->y - win->view_wrap->y;
	int relx = e->x - win->prevx;
	int rely = e->y - win->prevy;

	handled = view_mouse_motion(&win->view_wrap->view, e->state, x, y, relx, rely);

	win->prevx = e->x;
	win->prevy = e->y;
	return handled;
}

static bool
window_mouse_release(window_t *win, XEvent *ev)
{
	XButtonEvent *e = &ev->xbutton;
	int x = e->x - win->view_wrap->x;
	int y = e->y - win->view_wrap->y;
	return view_mouse_release(&win->view_wrap->view, e->button, x, y);
}

void
window_run(window_t *win)
{
	fd_set rfd;
	int xfd = XConnectionNumber(win->display);
	struct timespec now, prev;
	struct timespec drawtime = {.tv_nsec = 0};
	struct timespec *tv = &drawtime;
	bool draw_request = false;

	clock_gettime(CLOCK_MONOTONIC, &prev);
	XEvent ev;
	while(win->run) {
		FD_ZERO(&rfd);
		FD_SET(xfd, &rfd);
		DIEIF(pselect(xfd+1, &rfd, NULL, NULL, tv, NULL) < 0 && errno != EINTR);

		while(XPending(win->display)) {
			bool handled = false;
			XNextEvent(win->display, &ev);
			switch(ev.type) {
			case DestroyNotify:
				win->run = false;
				return;
			case KeyPress:
				handled = window_keypress(win, &ev);
				break;
			case ButtonPress:
				handled = window_mouse_press(win, &ev);
				break;
			case ButtonRelease:
				handled = window_mouse_release(win, &ev);
				break;
			case MotionNotify:
				handled = window_mouse_motion(win, &ev);
				break;
			case ConfigureNotify:
				handled = window_resize(win, &ev);
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
		window_redraw(win);
		XFlush(win->display);
		clock_gettime(CLOCK_MONOTONIC, &prev);
		tv = NULL;
	}
}
