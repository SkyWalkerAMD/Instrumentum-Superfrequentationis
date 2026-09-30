/* SPDX-License-Identifier: GPL-2.0-only
 * Inspect actual X11/Xwayland windows without the removed EL10 xwininfo tool.
 * CI only: built on each target, never installed in the OCTool package.
 */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

static int failed;
static int xerror(Display *display, XErrorEvent *event)
{
    (void)display;
    /* A client may close between QueryTree and GetWindowAttributes. */
    if (event->error_code != BadWindow) failed = 1;
    return 0;
}

static void inspect(Display *display, Window window, unsigned long pid,
                    Atom pid_atom, Atom name_atom, Atom utf8_atom, unsigned depth)
{
    XWindowAttributes attr;
    Atom type;
    int format;
    unsigned long count, remaining;
    unsigned char *data = NULL;
    if (depth > 64) { failed = 1; return; }
    if (XGetWindowAttributes(display, window, &attr) && attr.map_state == IsViewable &&
        XGetWindowProperty(display, window, pid_atom, 0, 1, False, XA_CARDINAL,
                           &type, &format, &count, &remaining, &data) == Success) {
        int matches = type == XA_CARDINAL && format == 32 && count == 1 &&
                      data && *(unsigned long *)data == pid;
        if (data) XFree(data);
        data = NULL;
        if (matches && XGetWindowProperty(display, window, name_atom, 0, 4096,
                False, utf8_atom, &type, &format, &count, &remaining, &data) == Success &&
                type == utf8_atom && format == 8 && remaining == 0 && data) {
            printf("%lx\t", window);
            /* Hex transport keeps arbitrary window titles out of line syntax. */
            for (unsigned long i = 0; i < count; ++i) printf("%02x", data[i]);
            putchar('\n');
        }
        if (data) XFree(data);
    }
    Window root, parent, *children = NULL;
    unsigned n = 0;
    if (XQueryTree(display, window, &root, &parent, &children, &n))
        for (unsigned i = 0; i < n; ++i)
            inspect(display, children[i], pid, pid_atom, name_atom, utf8_atom, depth + 1);
    if (children) XFree(children);
}

int main(int argc, char **argv)
{
    char *end;
    if (argc != 2 || argv[1][0] < '1' || argv[1][0] > '9') return 2;
    errno = 0;
    unsigned long pid = strtoul(argv[1], &end, 10);
    if (errno || *end || !pid) return 2;
    Display *display = XOpenDisplay(NULL);
    if (!display) { fputs("window-probe: cannot open DISPLAY\n", stderr); return 1; }
    XSetErrorHandler(xerror);
    inspect(display, DefaultRootWindow(display), pid,
            XInternAtom(display, "_NET_WM_PID", False),
            XInternAtom(display, "_NET_WM_NAME", False),
            XInternAtom(display, "UTF8_STRING", False), 0);
    XCloseDisplay(display);
    return failed ? 1 : 0;
}
