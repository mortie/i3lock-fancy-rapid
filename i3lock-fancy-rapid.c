#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <unistd.h>
#include <sys/wait.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <omp.h>

void box_blur_h(uint32_t *dest, uint32_t *src, int height, int width,
                int radius)
{
    double coeff = 1.0 / (radius * 2 + 1);
#pragma omp parallel for
    for (int i = 0; i < height; ++i) {
        int iwidth = i * width;
        double r_acc = 0.0;
        double g_acc = 0.0;
        double b_acc = 0.0;
        for (int j = -radius; j < width; ++j) {
            if (j - radius - 1 >= 0) {
                int index = iwidth + j - radius - 1;
                r_acc -= coeff * ((src[index] & 0xff0000) >> 16);
                g_acc -= coeff * ((src[index] & 0x00ff00) >> 8);
                b_acc -= coeff * ((src[index] & 0x0000ff));
            }
            if (j + radius < width) {
                int index = iwidth + j + radius;
                r_acc += coeff * ((src[index] & 0xff0000) >> 16);
                g_acc += coeff * ((src[index] & 0x00ff00) >> 8);
                b_acc += coeff * ((src[index] & 0x0000ff));
            }
            if (j < 0)
                continue;
            int index = iwidth + j;
            dest[index] = 0 |
                (((uint32_t)(r_acc + 0.5) & 0xff) << 16) |
                (((uint32_t)(g_acc + 0.5) & 0xff) << 8) |
                (((uint32_t)(b_acc + 0.5) & 0xff));
        }
    }
}

void box_blur_v(uint32_t *dest, uint32_t *src, int height, int width,
                int radius)
{
    double coeff = 1.0 / (radius * 2 + 1);
#pragma omp parallel for
    for (int j = 0; j < width; ++j) {
        double r_acc = 0.0;
        double g_acc = 0.0;
        double b_acc = 0.0;
        for (int i = -radius; i < height; ++i) {
            if (i - radius - 1 >= 0) {
                int index = (i - radius - 1) * width + j;
                r_acc -= coeff * ((src[index] & 0xff0000) >> 16);
                g_acc -= coeff * ((src[index] & 0x00ff00) >> 8);
                b_acc -= coeff * ((src[index] & 0x0000ff));
            }
            if (i + radius < height) {
                int index = (i + radius) * width + j;
                r_acc += coeff * ((src[index] & 0xff0000) >> 16);
                g_acc += coeff * ((src[index] & 0x00ff00) >> 8);
                b_acc += coeff * ((src[index] & 0x0000ff));
            }
            if (i < 0)
                continue;
            int index = i * width + j;
            dest[index] = 0 |
                (((uint32_t)(r_acc + 0.5) & 0xff) << 16) |
                (((uint32_t)(g_acc + 0.5) & 0xff) << 8) |
                (((uint32_t)(b_acc + 0.5) & 0xff));
        }
    }
}

void box_blur_once(uint32_t *dest, uint32_t *src, uint32_t *scratch,
        int height, int width, int radius)
{
    box_blur_h(scratch, src, height, width, radius);
    box_blur_v(dest, scratch, height, width, radius);
}

void box_blur(uint32_t *dest, uint32_t *src, int height, int width,
              int radius, int times)
{
    uint32_t *origdest = dest;

    uint32_t *scratch = malloc(width * height * sizeof(*scratch));
    box_blur_once(dest, src, scratch, height, width, radius);
    for (int i = 0; i < times - 1; ++i) {
        uint32_t *tmp = src;
        src = dest;
        dest = tmp;
        box_blur_once(dest, src, scratch, height, width, radius);
    }
    free(scratch);

    // We're flipping between using dest and src;
    // if the last buffer we used was src, copy that over to dest.
    if (dest != origdest)
        memcpy(origdest, dest, width * height * sizeof(*dest));
}

int main(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s radius times [OPTIONS]\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    Display *display = XOpenDisplay(NULL);
    Window root = XDefaultRootWindow(display);
    XWindowAttributes gwa;
    XGetWindowAttributes(display, root, &gwa);
    int height = gwa.height / SCALE;
    int width = gwa.width / SCALE;
    uint32_t *preblur = malloc(height * width * sizeof(*preblur));
    XImage *image = XGetImage(display, root, 0, 0, gwa.width, gwa.height, AllPlanes,
                              ZPixmap);

#pragma omp parallel for
    for (int i = 0; i < height; ++i) {
        int iwidth = i * width;
        for (int j = 0; j < width; ++j) {
            int index = iwidth + j;
            unsigned long pixel = XGetPixel(image, j * SCALE, i * SCALE);
            preblur[index] = pixel & 0x00ffffff;
        }
    }
    XDestroyImage(image);
    XDestroyWindow(display, root);
    XCloseDisplay(display);

    uint32_t *postblur = malloc(height * width * sizeof(*postblur));
    box_blur(postblur, preblur, height, width, atoi(argv[1]), atoi(argv[2]));
    free(preblur);

    // Upscale
    uint32_t *upscaled;
    if (SCALE == 1) {
        upscaled = postblur;
    } else {
        upscaled = malloc(height * SCALE * width * SCALE * sizeof(*upscaled));
#pragma omp parallel for
        for (int i = 0; i < height * SCALE; ++i) {
            for (int j = 0; j < width * SCALE; ++j) {
                upscaled[i * width * SCALE + j] = postblur[(i / SCALE) * width + (j / SCALE)];
            }
        }
    }

    int fds[2];
    pipe(fds);
    if (fork()) {
        write(fds[1], upscaled, height * SCALE * width * SCALE * sizeof(*upscaled));
        int status;
        wait(&status);
        exit(WEXITSTATUS(status));
    } else {
        dup2(fds[0], STDIN_FILENO);
        char geometry[32];
        snprintf(geometry, sizeof(geometry), "%ix%i:native", width * SCALE, height * SCALE);

        int argskip = 3;
        char *new_argv[6 + (argc - argskip)];
        new_argv[0] = "i3lock";
        new_argv[1] = "-i";
        new_argv[2] = "/dev/stdin";
        new_argv[3] = "-r";
        new_argv[4] = geometry;
        int idx = 5;
        for (int i = 0; i < argc - argskip; ++i)
            new_argv[idx++] = argv[i + argskip];
        new_argv[idx++] = NULL;
        execvp(new_argv[0], new_argv);
        exit(EXIT_FAILURE);
    }
}
