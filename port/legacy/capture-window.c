/* Capture an already visible diagnostic window, without sending input. */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned char channel(unsigned long pixel, unsigned long mask)
{
    if (!mask) return 0;
    while (!(mask & 1)) { mask >>= 1; pixel >>= 1; }
    return (unsigned char)(((pixel & mask) * 255) / mask);
}

int main(int argc, char **argv)
{
    char *end;
    unsigned long id;
    Display *display;
    XWindowAttributes attr;
    XImage *image;
    FILE *out;
    int x, y;
    if (argc != 3) return 2;
    errno = 0;
    id = strtoul(argv[1], &end, 16);
    if (errno || !id || *end) return 2;
    display = XOpenDisplay(NULL);
    if (!display) return 3;
    if (!XGetWindowAttributes(display, id, &attr) || attr.map_state != IsViewable ||
        attr.width <= 0 || attr.height <= 0 || attr.width > 8192 || attr.height > 8192) return 4;
    image = XGetImage(display, id, 0, 0, attr.width, attr.height, AllPlanes, ZPixmap);
    if (!image) return 5;
    out = fopen(argv[2], "wb");
    if (!out) return 6;
    fprintf(out, "P6\n%d %d\n255\n", attr.width, attr.height);
    for (y = 0; y < attr.height; y++) for (x = 0; x < attr.width; x++) {
        unsigned long pixel = XGetPixel(image, x, y);
        unsigned char rgb[3] = {channel(pixel, image->red_mask),
                               channel(pixel, image->green_mask),
                               channel(pixel, image->blue_mask)};
        if (fwrite(rgb, 1, 3, out) != 3) return 7;
    }
    if (fclose(out)) return 8;
    XDestroyImage(image);
    XCloseDisplay(display);
    return 0;
}
