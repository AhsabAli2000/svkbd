/* See LICENSE file for copyright and license details.
 *
 * To understand svkbd, start reading main().
 */
#include <locale.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xproto.h>
#include <X11/extensions/XTest.h>

/* macros */
#define MAX(a, b)       ((a) > (b) ? (a) : (b))
#define LENGTH(x)       (sizeof x / sizeof x[0])

/* enums */
enum { ColFG, ColBG, ColLast };

/* typedefs */
typedef unsigned int uint;
typedef unsigned long ulong;

typedef struct {
	ulong norm[ColLast];
	ulong press[ColLast];
	ulong hover[ColLast];
	Drawable drawable;
	GC gc;
	struct {
		int ascent;
		int descent;
		int height;
		XFontSet set;
		XFontStruct *xfont;
	} font;
} DC; /* draw context */

typedef struct {
	char *label;
	KeySym keysym;
	uint width;
	int x, y, w, h;
	Bool pressed;
} Key;

typedef struct {
	KeySym mod;
	uint button;
} Buttonmod;

/* function declarations */
static void buttonpress(XEvent *e);
static void buttonrelease(XEvent *e);
static void cleanup(void);
static void configurenotify(XEvent *e);
static void unmapnotify(XEvent *e);
static void die(const char *errstr, ...);
static void drawkeyboard(void);
static void drawkey(Key *k);
static void expose(XEvent *e);
static Key *findkey(int x, int y);
static ulong getcolor(const char *colstr);
static void initfont(const char *fontstr);
static void leavenotify(XEvent *e);
static void motionnotify(XEvent *e);
static void press(Key *k, KeySym mod);
static void run(void);
static void setup(void);
static int textnw(const char *text, uint len);
static void unpress();
static void updatekeys();

/* variables */
static int screen;
static int wx, wy, ww, wh;
static void (*handler[LASTEvent]) (XEvent *) = {
	[ButtonPress] = buttonpress,
	[ButtonRelease] = buttonrelease,
	[ConfigureNotify] = configurenotify,
	[UnmapNotify] = unmapnotify,
	[Expose] = expose,
	[LeaveNotify] = leavenotify,
	[MotionNotify] = motionnotify,
};
static Display *dpy;
static DC dc;
static Window root, win;
static Bool running = True;
static Key *hover = NULL;
static KeySym pressedmod = 0;
/* configuration, allows nested code to access above variables */
#include "config.h"

void
buttonpress(XEvent *e) {
	int i;
	XButtonPressedEvent *ev = &e->xbutton;
	Key *k;
	KeySym mod = 0;

	for(i = 0; i < LENGTH(buttonmods); i++)
		if(ev->button == buttonmods[i].button) {
			mod = buttonmods[i].mod;
			break;
		}
	if((k = findkey(ev->x, ev->y)))
		press(k, mod);
}

void
buttonrelease(XEvent *e) {
	XButtonPressedEvent *ev = &e->xbutton;
	Key *k;

	if((k = findkey(ev->x, ev->y)))
		unpress(k);
}

void
cleanup(void) {
	if(dc.font.set)
		XFreeFontSet(dpy, dc.font.set);
	else
		XFreeFont(dpy, dc.font.xfont);
	XFreePixmap(dpy, dc.drawable);
	XFreeGC(dpy, dc.gc);
	XDestroyWindow(dpy, win);
	XSync(dpy, False);
	XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
}

void
configurenotify(XEvent *e) {
	XConfigureEvent *ev = &e->xconfigure;

	if(ev->window == win && (ev->width != ww || ev->height != wh)) {
		ww = ev->width;
		wh = ev->height;
		XFreePixmap(dpy, dc.drawable);
		dc.drawable = XCreatePixmap(dpy, root, ww, wh, DefaultDepth(dpy, screen));
		updatekeys();
	}
}

void
die(const char *errstr, ...) {
	va_list ap;

	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
	exit(EXIT_FAILURE);
}

void
drawkeyboard(void) {
	int i;

	for(i = 0; i < LENGTH(keys); i++) {
		if(keys[i].keysym != 0)
			drawkey(&keys[i]);
	}
	XSync(dpy, False);
	XCopyArea(dpy, dc.drawable, win, dc.gc, 0, 0, ww, wh, 0, 0);
}

void
drawkey(Key *k) {
	int x, y, h, len;
	XRectangle r = { k->x, k->y, k->w, k->h};
	const char *l;
	ulong *col;

	if(k->pressed)
		col = dc.press;
	else if(hover == k)
		col = dc.hover;
	else
		col = dc.norm;
	XSetForeground(dpy, dc.gc, col[ColBG]);
	XFillRectangles(dpy, dc.drawable, dc.gc, &r, 1);
	XSetForeground(dpy, dc.gc, dc.norm[ColFG]);
	XDrawRectangles(dpy, dc.drawable, dc.gc, &r, 1);
	XSetForeground(dpy, dc.gc, col[ColFG]);
	if(k->label)
		l = k->label;
	else
		l = XKeysymToString(k->keysym);
	len = strlen(l);
	h = dc.font.ascent + dc.font.descent;
	y = k->y + (k->h / 2) - (h / 2) + dc.font.ascent;
	x = k->x + (k->w / 2) - (textnw(l, len) / 2);
	if(dc.font.set)
		XmbDrawString(dpy, dc.drawable, dc.font.set, dc.gc, x, y, l, len);
	else
		XDrawString(dpy, dc.drawable, dc.gc, x, y, l, len);
}

void
unmapnotify(XEvent *e) {
	running = False;
}

void
expose(XEvent *e) {
	XExposeEvent *ev = &e->xexpose;

	if(ev->count == 0 && (ev->window == win))
		drawkeyboard();
}

Key *
findkey(int x, int y) {
	int i;
	
	for(i = 0; i < LENGTH(keys); i++)
		if(keys[i].keysym && x > keys[i].x &&
				x < keys[i].x + keys[i].w &&
				y > keys[i].y && y < keys[i].y + keys[i].h)
			return &keys[i];
	return NULL;
}

ulong
getcolor(const char *colstr) {
	Colormap cmap = DefaultColormap(dpy, screen);
	XColor color;

	if(!XAllocNamedColor(dpy, cmap, colstr, &color, &color))
		die("error, cannot allocate color '%s'\n", colstr);
	return color.pixel;
}

void
initfont(const char *fontstr) {
	char *def, **missing;
	int i, n;

	missing = NULL;
	if(dc.font.set)
		XFreeFontSet(dpy, dc.font.set);
	dc.font.set = XCreateFontSet(dpy, fontstr, &missing, &n, &def);
	if(missing) {
		while(n--)
			fprintf(stderr, "svkbd: missing fontset: %s\n", missing[n]);
		XFreeStringList(missing);
	}
	if(dc.font.set) {
		XFontSetExtents *font_extents;
		XFontStruct **xfonts;
		char **font_names;
		dc.font.ascent = dc.font.descent = 0;
		font_extents = XExtentsOfFontSet(dc.font.set);
		n = XFontsOfFontSet(dc.font.set, &xfonts, &font_names);
		for(i = 0, dc.font.ascent = 0, dc.font.descent = 0; i < n; i++) {
			dc.font.ascent = MAX(dc.font.ascent, (*xfonts)->ascent);
			dc.font.descent = MAX(dc.font.descent,(*xfonts)->descent);
			xfonts++;
		}
	}
	else {
		if(dc.font.xfont)
			XFreeFont(dpy, dc.font.xfont);
		dc.font.xfont = NULL;
		if(!(dc.font.xfont = XLoadQueryFont(dpy, fontstr))
		&& !(dc.font.xfont = XLoadQueryFont(dpy, "fixed")))
			die("error, cannot load font: '%s'\n", fontstr);
		dc.font.ascent = dc.font.xfont->ascent;
		dc.font.descent = dc.font.xfont->descent;
	}
	dc.font.height = dc.font.ascent + dc.font.descent;
}

void
leavenotify(XEvent *e) {
	unpress(NULL);
	if(!hover)
		return;
	hover = NULL;
	drawkeyboard();
}

void
motionnotify(XEvent *e) {
	XMotionEvent *ev = &e->xmotion;
	Key *h = findkey(ev->x, ev->y), *oh;

	if(h != hover) {
		oh = hover;;
		hover = h;
		if(oh)
			drawkey(oh);
		if(hover)
			drawkey(hover);
	}
	XCopyArea(dpy, dc.drawable, win, dc.gc, 0, 0, ww, wh, 0, 0);
}

void
press(Key *k, KeySym mod) {
	int i;
	k->pressed = !k->pressed;

	if(!IsModifierKey(k->keysym)) {
		for(i = 0; i < LENGTH(keys); i++)
			if(keys[i].pressed && IsModifierKey(keys[i].keysym))
				XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, keys[i].keysym), True, 0);
		pressedmod = mod;
		if(pressedmod)
			XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, mod), True, 0);
		XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, k->keysym), True, 0);
	}
	drawkey(k);
	XCopyArea(dpy, dc.drawable, win, dc.gc, k->x, k->y, k->w, k->h, k->x, k->y);
}

void
run(void) {
	XEvent ev;

	/* main event loop, also reads status text from stdin */
	XSync(dpy, False);
	while(running) {
		XNextEvent(dpy, &ev);
		if(handler[ev.type])
			(handler[ev.type])(&ev); /* call handler */
	}
}

void
setup(void) {
	int i;
	XWMHints *wmh;

	/* init screen */
	screen = DefaultScreen(dpy);
	root = RootWindow(dpy, screen);
	initfont(font);

	/* init appearance */
	ww = DisplayWidth(dpy, screen);
	wh = 200;
	wx = 0;
	wy = DisplayHeight(dpy, screen) - wh;
	dc.norm[ColBG] = getcolor(normbgcolor);
	dc.norm[ColFG] = getcolor(normfgcolor);
	dc.press[ColBG] = getcolor(pressbgcolor);
	dc.press[ColFG] = getcolor(pressfgcolor);
	dc.hover[ColBG] = getcolor(hovbgcolor);
	dc.hover[ColFG] = getcolor(hovfgcolor);
	dc.drawable = XCreatePixmap(dpy, root, ww, wh, DefaultDepth(dpy, screen));
	dc.gc = XCreateGC(dpy, root, 0, 0);
	if(!dc.font.set)
		XSetFont(dpy, dc.gc, dc.font.xfont->fid);
	for(i = 0; i < LENGTH(keys); i++)
		keys[i].pressed = 0;

	win = XCreateSimpleWindow(dpy, root, wx, wy, ww, wh, 0, dc.norm[ColFG], dc.norm[ColBG]);
	XSelectInput(dpy, win, StructureNotifyMask|PointerMotionMask|
			ButtonReleaseMask|ButtonPressMask|ExposureMask|
			LeaveWindowMask);
	wmh = XAllocWMHints();
	wmh->input = False;
	wmh->flags = InputHint;
	XSetWMHints(dpy, win, wmh);
	XFree(wmh);
	XMapRaised(dpy, win);
	updatekeys();
	drawkeyboard();
}

int
textnw(const char *text, uint len) {
	XRectangle r;

	if(dc.font.set) {
		XmbTextExtents(dc.font.set, text, len, NULL, &r);
		return r.width;
	}
	return XTextWidth(dc.font.xfont, text, len);
}

void
unpress() {
	int i;

	for(i = 0; i < LENGTH(keys); i++)
		if(keys[i].pressed && !IsModifierKey(keys[i].keysym)) {
			XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, keys[i].keysym), False, 0);
			keys[i].pressed = 0;
			break;
		}
	if(i !=  LENGTH(keys)) {
		for(i = 0; i < LENGTH(keys); i++) {
			if(pressedmod)
				XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, pressedmod), False, 0);
			pressedmod = 0;
			if(keys[i].pressed) {
				XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, keys[i].keysym), False, 0);
				keys[i].pressed = 0;
			}
		}
		drawkeyboard();
	}
}

void
updatekeys() {
	int rows, i, j;
	int x = 0, y = 0, h, base;

	for(i = 0, rows = 1; i < LENGTH(keys); i++)
		if(keys[i].keysym == 0)
			rows++;
	h = wh / rows;
	for(i = 0; i < LENGTH(keys); i++, rows--) {
		for(j = i, base = 0; j < LENGTH(keys) && keys[j].keysym != 0; j++)
			base += keys[j].width;
		for(x = 0; i < LENGTH(keys) && keys[i].keysym != 0; i++) {
			keys[i].x = x;
			keys[i].y = y;
			keys[i].w = keys[i].width * (ww - 1) / base;
			if(rows != 1)
				keys[i].h = h;
			else
				keys[i].h = (wh - 1) - y;
			x += keys[i].w;
		}
		if(base != 0)
			keys[i - 1].w = (ww - 1) - keys[i - 1].x;
		y += h;
	}
}

int
main(int argc, char *argv[]) {
	if(argc == 2 && !strcmp("-v", argv[1]))
		die("svkc-"VERSION", © 2006-2008 svkbd engineers, see LICENSE for details\n");
	else if(argc != 1)
		die("usage: svkbd [-v]\n");
	if(!setlocale(LC_CTYPE, "") || !XSupportsLocale())
		fprintf(stderr, "warning: no locale support\n");
	if(!(dpy = XOpenDisplay(0)))
		die("svkbd: cannot open display\n");
	setup();
	run();
	cleanup();
	XCloseDisplay(dpy);
	return 0;
}
