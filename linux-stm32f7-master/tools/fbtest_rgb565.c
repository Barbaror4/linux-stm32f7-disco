/*
 * fbtest_rgb565.c - STM32F746 LCD Framebuffer (RGB565) test utility
 *
 * Tests /dev/fb0 via standard open(), ioctl(), write(), and fsync().
 * Renders 8 horizontal color bars or solid color fills.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <stdint.h>

#define DEFAULT_FB_DEV "/dev/fb0"

/* 16-bit RGB565 colors (5-bit Red, 6-bit Green, 5-bit Blue) */
#define COLOR_RED     0xF800
#define COLOR_GREEN   0x07E0
#define COLOR_BLUE    0x001F
#define COLOR_YELLOW  0xFFE0
#define COLOR_CYAN    0x07FF
#define COLOR_MAGENTA 0xF81F
#define COLOR_WHITE   0xFFFF
#define COLOR_BLACK   0x0000

static const struct {
    const char *name;
    uint16_t val;
} bar_table[8] = {
    {"Red",     COLOR_RED},
    {"Green",   COLOR_GREEN},
    {"Blue",    COLOR_BLUE},
    {"Yellow",  COLOR_YELLOW},
    {"Cyan",    COLOR_CYAN},
    {"Magenta", COLOR_MAGENTA},
    {"White",   COLOR_WHITE},
    {"Black",   COLOR_BLACK},
};

int main(int argc, char *argv[])
{
    const char *fb_path = DEFAULT_FB_DEV;
    int fd;
    struct fb_fix_screeninfo finfo;
    struct fb_var_screeninfo vinfo;
    uint32_t width, height, bpp;
    uint16_t *line_buf;
    uint32_t line_bytes;
    int bar_idx, line_in_bar, bar_height;
    int total_lines;
    uint32_t x, y;

    printf("=== STM32F746 Framebuffer Test (RGB565) ===\n");

    fd = open(fb_path, O_RDWR);
    if (fd < 0) {
        perror("Failed to open framebuffer device");
        fprintf(stderr, "Check if %s exists and is registered.\n", fb_path);
        return 1;
    }

    if (ioctl(fd, FBIOGET_FSCREENINFO, &finfo) < 0) {
        perror("ioctl FBIOGET_FSCREENINFO failed");
        close(fd);
        return 1;
    }

    if (ioctl(fd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
        perror("ioctl FBIOGET_VSCREENINFO failed");
        close(fd);
        return 1;
    }

    width  = vinfo.xres;
    height = vinfo.yres;
    bpp    = vinfo.bits_per_pixel;

    printf("Device: %s\n", finfo.id);
    printf("Resolution: %u x %u, %u bpp\n", width, height, bpp);
    printf("Line length: %u bytes, smem_len: %u bytes\n",
           finfo.line_length, finfo.smem_len);
    printf("Buffer physical base: 0x%08lx\n", (unsigned long)finfo.smem_start);

    if (bpp != 16) {
        fprintf(stderr, "Warning: Expected 16 bpp RGB565, reported %u bpp\n", bpp);
    }

    line_bytes = width * 2;
    line_buf = (uint16_t *)malloc(line_bytes);
    if (!line_buf) {
        fprintf(stderr, "Out of memory allocating line buffer\n");
        close(fd);
        return 1;
    }

    /* Rewind to beginning of framebuffer */
    lseek(fd, 0, SEEK_SET);

    /* Check if a single solid color was requested via argument */
    if (argc > 1) {
        uint16_t solid_color = COLOR_BLACK;
        const char *arg = argv[1];
        if (strcasecmp(arg, "red") == 0) solid_color = COLOR_RED;
        else if (strcasecmp(arg, "green") == 0) solid_color = COLOR_GREEN;
        else if (strcasecmp(arg, "blue") == 0) solid_color = COLOR_BLUE;
        else if (strcasecmp(arg, "yellow") == 0) solid_color = COLOR_YELLOW;
        else if (strcasecmp(arg, "cyan") == 0) solid_color = COLOR_CYAN;
        else if (strcasecmp(arg, "magenta") == 0) solid_color = COLOR_MAGENTA;
        else if (strcasecmp(arg, "white") == 0) solid_color = COLOR_WHITE;
        else if (strcasecmp(arg, "black") == 0) solid_color = COLOR_BLACK;
        else {
            solid_color = (uint16_t)strtoul(arg, NULL, 0);
        }

        printf("Filling screen with solid color: 0x%04X (%s)...\n", solid_color, arg);
        for (x = 0; x < width; x++)
            line_buf[x] = solid_color;

        for (y = 0; y < height; y++) {
            if (write(fd, line_buf, line_bytes) != (ssize_t)line_bytes) {
                perror("write failed");
                break;
            }
        }
    } else {
        /* Default: 8 horizontal color bars */
        bar_height = height / 8; /* 272 / 8 = 34 lines */
        printf("Rendering 8 horizontal color bars (each %d lines high)...\n", bar_height);

        total_lines = 0;
        for (bar_idx = 0; bar_idx < 8; bar_idx++) {
            uint16_t c = bar_table[bar_idx].val;
            int lines_this_bar = (bar_idx == 7) ? (height - total_lines) : bar_height;

            for (x = 0; x < width; x++)
                line_buf[x] = c;

            for (line_in_bar = 0; line_in_bar < lines_this_bar; line_in_bar++) {
                if (write(fd, line_buf, line_bytes) != (ssize_t)line_bytes) {
                    perror("write error");
                    break;
                }
                total_lines++;
            }
            printf("  Bar %d: %-8s (0x%04X) -> lines %d..%d\n",
                   bar_idx, bar_table[bar_idx].name, c,
                   total_lines - lines_this_bar, total_lines - 1);
        }
    }

    /* Synchronize cached writes to hardware */
    fsync(fd);
    printf("Sync complete. Test pattern displayed successfully on LCD!\n");

    free(line_buf);
    close(fd);
    return 0;
}
