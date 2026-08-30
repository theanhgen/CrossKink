/* fbramp - show all 16 panel levels so four good ones can be chosen.
 *
 * CrossInk renders 4 levels (1bpp base + LSB/MSB planes). The panel offers 16.
 * We currently use 0x0/0x5/0xA/0xF because that is what the stock UI uses, but
 * e-ink response is not linear, so evenly spaced nibbles are not evenly spaced
 * greys. This displays every level to find which four are actually distinct.
 *
 * Layout, top to bottom: 16 bands of 50px, nibble 0 (top) through 15 (bottom).
 * Polarity is inverted on this panel, so 0x0 is WHITE and 0xF is BLACK - the
 * ramp reads light at the top, dark at the bottom.
 *
 * A ladder down the left edge alternates black/white per band so the
 * boundaries stay countable even where neighbouring bands look identical -
 * which is exactly the information wanted. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>

#define FBIO_EINK_UPDATE_DISPLAY 0x46DB
#define FX_FULL 1

static unsigned char *fb;
static unsigned int ll, xres, yres;

static void px(unsigned x, unsigned y, unsigned char nib) {
    if (x >= xres || y >= yres) return;
    unsigned char *b = fb + (size_t)y * ll + (x >> 1);
    if (x & 1) *b = (unsigned char)((*b & 0xF0) | (nib & 0x0F));
    else       *b = (unsigned char)((*b & 0x0F) | ((nib & 0x0F) << 4));
}


/* 3x5 digit font, scaled up. Hand-coded because this tool runs before any of
 * CrossInk's font machinery and must not depend on it. */
static const unsigned char DIGITS[10][5] = {
    {0x7,0x5,0x5,0x5,0x7}, /* 0 */
    {0x2,0x6,0x2,0x2,0x7}, /* 1 */
    {0x7,0x1,0x7,0x4,0x7}, /* 2 */
    {0x7,0x1,0x7,0x1,0x7}, /* 3 */
    {0x5,0x5,0x7,0x1,0x1}, /* 4 */
    {0x7,0x4,0x7,0x1,0x7}, /* 5 */
    {0x7,0x4,0x7,0x5,0x7}, /* 6 */
    {0x7,0x1,0x1,0x1,0x1}, /* 7 */
    {0x7,0x5,0x7,0x5,0x7}, /* 8 */
    {0x7,0x5,0x7,0x1,0x7}, /* 9 */
};

static void draw_digit(unsigned x0, unsigned y0, int d, unsigned scale, unsigned char nib) {
    if (d < 0 || d > 9) return;
    for (unsigned r = 0; r < 5; r++)
        for (unsigned c = 0; c < 3; c++)
            if (DIGITS[d][r] & (1u << (2 - c)))
                for (unsigned sy = 0; sy < scale; sy++)
                    for (unsigned sx = 0; sx < scale; sx++)
                        px(x0 + c * scale + sx, y0 + r * scale + sy, nib);
}

static void draw_number(unsigned x0, unsigned y0, int n, unsigned scale, unsigned char nib) {
    if (n >= 10) {
        draw_digit(x0, y0, n / 10, scale, nib);
        draw_digit(x0 + 4 * scale, y0, n % 10, scale, nib);
    } else {
        draw_digit(x0 + 2 * scale, y0, n, scale, nib);
    }
}

int main(int argc, char **argv) {
    const int hold = (argc > 1) ? atoi(argv[1]) : 120;

    int fd = open("/dev/fb0", O_RDWR);
    if (fd < 0) { printf("cannot open /dev/fb0\n"); return 1; }
    struct fb_var_screeninfo v; struct fb_fix_screeninfo f;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &v) < 0 || ioctl(fd, FBIOGET_FSCREENINFO, &f) < 0) {
        printf("ioctl failed\n"); return 1;
    }
    xres = v.xres; yres = v.yres; ll = f.line_length;
    fb = mmap(NULL, f.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (fb == MAP_FAILED) { printf("mmap failed\n"); return 1; }

    const unsigned bandH = yres / 16;      /* 800/16 = 50 */
    const unsigned labelW = 80;            /* white gutter carrying the number */
    const unsigned scale  = 6;             /* digits are 3x5 -> 18x30 px */

    for (unsigned band = 0; band < 16; band++) {
        const unsigned char nib = (unsigned char)band;
        const unsigned y0 = band * bandH;
        for (unsigned y = y0; y < y0 + bandH && y < yres; y++) {
            /* gutter stays white (0x0 on this inverted panel) so the black
             * label is legible against every band, including 0x0 itself -
             * hence the separating rule drawn below. */
            for (unsigned x = 0; x < labelW; x++) px(x, y, 0x0);
            for (unsigned x = labelW; x < xres; x++) px(x, y, nib);
        }
        /* rule between gutter and band, so band 0 (white) still has an edge */
        for (unsigned y = y0; y < y0 + bandH && y < yres; y++)
            for (unsigned x = labelW - 3; x < labelW; x++) px(x, y, 0xF);

        draw_number(12, y0 + (bandH - 5 * scale) / 2, (int)band, scale, 0xF);
    }

    if (ioctl(fd, FBIO_EINK_UPDATE_DISPLAY, FX_FULL) < 0) printf("update failed\n");
    printf("16 bands drawn, 0 (top) .. 15 (bottom), %ups each; holding %ds\n", bandH, hold);
    fflush(stdout);
    sleep(hold);

    for (unsigned y = 0; y < yres; y++) memset(fb + (size_t)y * ll, 0x00, xres / 2);
    ioctl(fd, FBIO_EINK_UPDATE_DISPLAY, FX_FULL);
    munmap(fb, f.smem_len); close(fd);
    printf("done\n");
    return 0;
}
