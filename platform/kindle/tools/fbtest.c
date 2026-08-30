/* fbtest - settle the framebuffer constants by drawing known values.
 *
 * Writes directly to /dev/fb0 in the 4bpp packed format HalDisplay uses, so
 * whatever appears on the panel is a verdict on that code, not a simulation
 * of it.
 *
 * What it answers:
 *   - POLARITY (kInvertNibbles): 16 vertical bands, nibble 0 on the left
 *     through 15 on the right. If 0 renders BLACK, polarity is correct as
 *     shipped. If 0 renders WHITE, flip kInvertNibbles.
 *   - GEOMETRY: a border frame plus quadrant blocks. Any skew, wrap or tearing
 *     means line_length is being handled wrongly.
 *
 * What it cannot answer: nibble ORDER (kHighNibbleIsLeftPixel). Swapping the
 * two pixels inside a byte maps a pattern P(x) to P(x^1), and every pattern
 * coarse enough to see on e-ink is symmetric under that. It shows up instead
 * as subtly serrated text once the reader renders - judge it there.
 *
 * Restores a white screen before exiting; the framework repaints over it. */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <stdlib.h>

#define FBIO_EINK_UPDATE_DISPLAY 0x46DB
#define FX_UPDATE_FULL 1

static unsigned char *fb;
static unsigned int line_len, xres, yres;

/* One pixel, 4bpp packed, 2 px per byte, high nibble = left pixel. */
static void px(int x, int y, unsigned char nib) {
    if (x < 0 || y < 0 || (unsigned)x >= xres || (unsigned)y >= yres) return;
    unsigned char *b = fb + (size_t)y * line_len + (x >> 1);
    if (x & 1) *b = (unsigned char)((*b & 0xF0) | (nib & 0x0F));
    else       *b = (unsigned char)((*b & 0x0F) | ((nib & 0x0F) << 4));
}

static void rect(int x0, int y0, int w, int h, unsigned char nib) {
    for (int y = y0; y < y0 + h; y++)
        for (int x = x0; x < x0 + w; x++) px(x, y, nib);
}

int main(int argc, char **argv) {
    int hold = (argc > 1) ? atoi(argv[1]) : 25;

    int fd = open("/dev/fb0", O_RDWR);
    if (fd < 0) { printf("cannot open /dev/fb0\n"); return 1; }

    struct fb_var_screeninfo v;
    struct fb_fix_screeninfo f;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &v) < 0 || ioctl(fd, FBIOGET_FSCREENINFO, &f) < 0) {
        printf("ioctl failed\n"); close(fd); return 1;
    }
    xres = v.xres; yres = v.yres; line_len = f.line_length;
    printf("panel %ux%u bpp=%u line_length=%u\n", xres, yres, v.bits_per_pixel, line_len);
    if (v.bits_per_pixel != 4) { printf("not 4bpp - aborting\n"); close(fd); return 1; }

    fb = mmap(NULL, f.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (fb == MAP_FAILED) { printf("mmap failed\n"); close(fd); return 1; }

    /* white ground */
    rect(0, 0, xres, yres, 0xF);

    /* 16 bands, nibble 0 (left) .. 15 (right) */
    {
        int bw = xres / 16;
        for (int i = 0; i < 16; i++) rect(i * bw, 120, bw, 300, (unsigned char)i);
    }

    /* border frame: geometry check - must be even on all four sides */
    rect(0, 0, xres, 8, 0x0);
    rect(0, yres - 8, xres, 8, 0x0);
    rect(0, 0, 8, yres, 0x0);
    rect(xres - 8, 0, 8, yres, 0x0);

    /* solid reference blocks, low on screen */
    rect(40,  520, 240, 160, 0x0);   /* pure 0 */
    rect(320, 520, 240, 160, 0xF);   /* pure 15 */

    /* a mid-grey pair to show the panel really has levels between */
    rect(40,  700, 240, 60, 0x5);
    rect(320, 700, 240, 60, 0xA);

    if (ioctl(fd, FBIO_EINK_UPDATE_DISPLAY, FX_UPDATE_FULL) < 0)
        printf("update ioctl failed\n");

    printf("pattern drawn - holding %ds\n", hold);
    fflush(stdout);
    sleep(hold);

    rect(0, 0, xres, yres, 0xF);
    ioctl(fd, FBIO_EINK_UPDATE_DISPLAY, FX_UPDATE_FULL);

    munmap(fb, f.smem_len);
    close(fd);
    printf("done\n");
    return 0;
}
