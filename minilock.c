/*
 * Copyright (C) 2015 Jan Synáček
 *
 * Author: Jan Synáček <jan.synacek@gmail.com>
 * URL: https://github.com/jsynacek/minilock
 * Created: Apr 2015
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth
 * Floor, Boston, MA 02110-1301, USA.
 */

#define _XOPEN_SOURCE 500
#include <pwd.h>
#include <shadow.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrandr.h>

static Bool has_xrandr;

static void
die(const char *err, ...)
{
	va_list va;

	va_start(va, err);
	vfprintf(stderr, err, va);
	va_end(va);

	exit(EXIT_FAILURE);
}

static int
auth(const char *real_pwd, const char *pwd)
{
	return strcmp(crypt(pwd, real_pwd), real_pwd) == 0;
}

static XColor
get_color(Display *dpy, const char *colorstr)
{
	XColor color;
	Colormap cmap;

	cmap = DefaultColormap(dpy, 0);
	XParseColor(dpy, cmap, colorstr, &color);
	XAllocColor(dpy, cmap, &color);

	return color;
}

static int
get_screen_resolution(Display *dpy, int scr, int *width, int *height)
{
	XRRScreenSize *sizes;
	int nsizes;

	if (has_xrandr) {
		sizes = XRRSizes(dpy, scr, &nsizes);
		if (nsizes <= 0)
			return -1;
		*width  = sizes[0].width;
		*height = sizes[0].height;
	} else {
		*width  = DisplayWidth(dpy, scr);
		*height = DisplayHeight(dpy, scr);
	}

	return 0;
}

static void
draw_dialog(Display *dpy, Window win, Pixmap pixmap, int pwdlen, int failed)
{
	GC gc, gcp;
	XColor col;
	char mask[256];
	char *user, *user_label;
	char *pwd_label = "password:";
	int scr, lx = 15, ly = 15, ystep = 20;
	int pixmap_w = 300, pixmap_h = 150;
	int scr_w, scr_h;

	memset(mask, '*', pwdlen);
	mask[pwdlen] = '\0';
	user = getenv("USER");
	user_label = "user:";

	gc = XCreateGC(dpy, pixmap, 0, NULL);
	col = get_color(dpy, "#657b83"); /* solarized base00 */
	XSetForeground(dpy, gc, col.pixel);

	gcp = XCreateGC(dpy, pixmap, 0, NULL);
	col = get_color(dpy, "#eee8d5"); /* solarozed base2 */
	XSetForeground(dpy, gcp, col.pixel);
	XFillRectangle(dpy, pixmap, gcp, 0, 0, pixmap_w, pixmap_h);

	scr = DefaultScreen(dpy);
	get_screen_resolution(dpy, scr, &scr_w, &scr_h);
	lx = pixmap_w / 4;
	ly = pixmap_h / 4;
	/* user */
	XDrawString(dpy, pixmap, gc, lx, ly, user_label, strlen(user_label));
	XDrawString(dpy, pixmap, gc, lx + 80, ly, user, strlen(user));
	/* password */
	ly += ystep;
	XDrawString(dpy, pixmap, gc, lx, ly, pwd_label, strlen(pwd_label));
	XDrawString(dpy, pixmap, gc, lx + 80, ly, mask, pwdlen) ;
	/* possible failure info */
	if (failed) {
		GC gc_failed;
		XColor col;
		char *fail_label = "Authentication failed!";

		gc_failed = XCreateGC(dpy, pixmap, 0, NULL);
		col = get_color(dpy, "#dc322f"); /* solarized red */
		XSetForeground(dpy, gc_failed, col.pixel);
		ly += ystep;
		XDrawString(dpy, pixmap, gc_failed, lx, ly, fail_label, strlen(fail_label)) ;
		XFreeGC(dpy, gc_failed);
	}

	XCopyArea(dpy, pixmap, win, gc, 0, 0, pixmap_w, pixmap_h,
		  (scr_w - pixmap_w) / 2, (scr_h - pixmap_h) / 2);
	XFlush(dpy);
	XFreeGC(dpy, gc);
	XFreeGC(dpy, gcp);
}

static const char *
load_password(void)
{
	struct spwd *spwd;

	spwd = getspnam(getenv("USER"));
	if (!spwd)
		die("minilock: getspnam() failed! make the binary suid\n");

	if (geteuid() == 0 && setuid(getuid()) < 0)
		die("minilock: cannot drop privileges\n");

	return spwd->sp_pwdp;
}

static void
suspend()
{
	if (fork() == 0)
		execlp("systemctl", "systemctl", "suspend", NULL);
}

int
main(void)
{
	Display *dpy;
	int scr;
	Window win;
	XSetWindowAttributes swa;
	Pixmap pixmap;
	XColor col;
	XEvent ev;
	int running = 1, failed = 0;
	int i;
	const char *real_pwd;
	char pwd[256];
	int maj_ver, min_ver;

	real_pwd = load_password();

	dpy = XOpenDisplay(NULL);
	if (!dpy)
		die("minilock: opening X11 display failed\n");
	scr = DefaultScreen(dpy);

	has_xrandr = XRRQueryExtension(dpy, &maj_ver, &min_ver);
	if (!has_xrandr)
		die("minilock: no xrandr found");

	for (i = 0; i < 1000; i++) {
		if (XGrabKeyboard(dpy, DefaultRootWindow(dpy), True, GrabModeAsync, GrabModeAsync, CurrentTime) == GrabSuccess)
			break;
		usleep(1000);
	}

	swa.override_redirect = True;
	col = get_color(dpy, "#fdf6e3"); /* solarized base3 */
	swa.background_pixel = col.pixel;
	swa.event_mask = ExposureMask | KeyPressMask;
	win = XCreateWindow(dpy, RootWindow(dpy, scr), 0, 0,
			    DisplayWidth(dpy, scr), DisplayHeight(dpy, scr), 0,
			    DefaultDepth(dpy, scr), CopyFromParent, DefaultVisual(dpy, scr),
			    CWOverrideRedirect | CWBackPixel | CWEventMask, &swa);
	XMapRaised(dpy, win);
	XFlush(dpy);

	pixmap = XCreatePixmap(dpy, win, 300, 150, 24);
	i = 0;
	pwd[0] = '\0';
	while(running && !XNextEvent(dpy, &ev)) {
		if (ev.type == KeyPress) {
			KeySym ksym;
			char buf[16];
			int cnt;

			cnt = XLookupString(&ev.xkey, buf, sizeof buf, &ksym, NULL);

			if (IsModifierKey(ksym) || IsCursorKey(ksym) || IsFunctionKey(ksym))
				continue;

			failed = 0;
			/* special */
			if (ev.xkey.state == ControlMask && ksym == XK_s) {
				suspend();
				continue;
			}
			switch (ksym) {
			case XK_Return:
				if (auth(real_pwd, pwd))
					running = 0;
				else
					failed = 1;
				break;
			case XK_BackSpace:
				if (i > 0)
					pwd[--i] = '\0';
				break;
			case XK_Escape:
				i = 0;
				break;
			default:
				if (i + cnt < sizeof pwd) {
					memcpy(pwd + i, buf, cnt);
					i += cnt;
					pwd[i] = '\0';
				}
				break;
			}
			draw_dialog(dpy, win, pixmap, i, failed);
		} else if (ev.type == Expose) {
			draw_dialog(dpy, win, pixmap, i, failed);
		}
		if (failed)
			i = 0;
	}
	XCloseDisplay(dpy);

	return EXIT_SUCCESS;
}
