/* fbpolarity - one static image, held long enough to study.
 *
 * Earlier attempts asked the viewer to time flashes; e-ink holds its image
 * between phases, so that was unreadable. This draws ONE layout and leaves it
 * up for a minute:
 *
 *   top third     solid nibble 0x0
 *   middle third  16 bands, nibble 0 (left) .. 15 (right)
 *   bottom third  solid nibble 0xF
 *
 * If the TOP is black, polarity is correct as shipped (kInvertNibbles=false).
 * If the top is white, flip it. The bands then show how many distinct grey
 * levels the panel really renders.
 *
 * Uses memset row fills - the approach that demonstrably reached the panel in
 * the fbflash run - and writes only plane 0, which xres_virtual/yoffset show
 * is the visible one. */
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

int main(int argc, char **argv) {
    const int hold = (argc > 1) ? atoi(argv[1]) : 60;

    int fd = open("/dev/fb0", O_RDWR);
    if (fd < 0) { printf("cannot open /dev/fb0\n"); return 1; }

    struct fb_var_screeninfo v; struct fb_fix_screeninfo f;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &v) < 0 || ioctl(fd, FBIOGET_FSCREENINFO, &f) < 0) {
        printf("ioctl failed\n"); return 1;
    }
    const unsigned xres = v.xres, yres = v.yres, ll = f.line_length;
    unsigned char *fb = mmap(NULL, f.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (fb == MAP_FAILED) { printf("mmap failed\n"); return 1; }

    const unsigned third = yres / 3;
    const unsigned row_bytes = xres / 2;          /* 4bpp: 2 px per byte */
    const unsigned band_w = row_bytes / 16;       /* bytes per band */

    for (unsigned y = 0; y < yres; y++) {
        unsigned char *row = fb + (size_t)y * ll;
        if (y < third) {
            memset(row, 0x00, row_bytes);                  /* nibble 0 */
        } else if (y < third * 2) {
            for (unsigned b = 0; b < 16; b++) {
                const unsigned char nib = (unsigned char)b;
                memset(row + b * band_w, (nib << 4) | nib, band_w);
            }
        } else {
            memset(row, 0xFF, row_bytes);                  /* nibble 15 */
        }
    }

    if (ioctl(fd, FBIO_EINK_UPDATE_DISPLAY, FX_FULL) < 0) printf("update failed\n");
    printf("drawn: top=nibble0 middle=16 bands bottom=nibble15, holding %ds\n", hold);
    fflush(stdout);
    sleep(hold);

    for (unsigned y = 0; y < yres; y++) memset(fb + (size_t)y * ll, 0xFF, row_bytes);
    ioctl(fd, FBIO_EINK_UPDATE_DISPLAY, FX_FULL);

    munmap(fb, f.smem_len); close(fd);
    printf("done\n");
    return 0;
}
