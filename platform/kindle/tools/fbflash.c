/* fbflash - answer one question: do our framebuffer writes reach the panel?
 *
 * fbtest drew a detailed pattern and nothing appeared, while the program
 * reported success. Rather than guess, this walks through the plausible
 * causes one at a time, each as a full-screen black flash that is impossible
 * to miss or mistake for the updater's own screen.
 *
 *   Phase 1  write plane 0 (offset 0)          + FBIO_EINK_UPDATE_DISPLAY
 *   Phase 2  write plane 1 (offset line*yres)  + FBIO_EINK_UPDATE_DISPLAY
 *   Phase 3  write the ENTIRE mapping          + FBIO_EINK_UPDATE_DISPLAY
 *   Phase 4  write the ENTIRE mapping          + /proc/eink_fb/update_display
 *   Phase 5  write the ENTIRE mapping          + FBIO_EINK_UPDATE_DISPLAY_AREA
 *
 * Each phase: go black, hold, go white, hold. Whichever phase visibly flashes
 * tells us which buffer and which trigger the panel actually honours.
 * Timings are printed so the log can be matched against what was seen. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>

#define FBIO_EINK_UPDATE_DISPLAY      0x46DB
#define FBIO_EINK_UPDATE_DISPLAY_AREA 0x46DD
#define FX_FULL 1

struct update_area {
    int x1, y1, x2, y2;
    int which_fx;
    unsigned char *buffer;
};

static int fd;
static unsigned char *fb;
static unsigned int line_len, xres, yres, smem;

static void trigger_ioctl(void)  { ioctl(fd, FBIO_EINK_UPDATE_DISPLAY, FX_FULL); }
static void trigger_proc(void)   { (void)system("echo 1 > /proc/eink_fb/update_display 2>/dev/null"); }
static void trigger_area(void) {
    struct update_area a;
    a.x1 = 0; a.y1 = 0; a.x2 = (int)xres; a.y2 = (int)yres;
    a.which_fx = FX_FULL; a.buffer = NULL;
    ioctl(fd, FBIO_EINK_UPDATE_DISPLAY_AREA, &a);
}

/* nib 0x0 = black, 0xF = white under the assumed polarity */
static void fill_plane(size_t off, size_t len, unsigned char nib) {
    if (off >= smem) return;
    if (off + len > smem) len = smem - off;
    memset(fb + off, (int)((nib << 4) | nib), len);
}

static void phase(int n, const char *what, size_t off, size_t len, void (*trig)(void)) {
    printf("phase %d: %s\n", n, what); fflush(stdout);
    fill_plane(off, len, 0x0); trig(); sleep(6);   /* black */
    fill_plane(off, len, 0xF); trig(); sleep(3);   /* white */
}

int main(void) {
    fd = open("/dev/fb0", O_RDWR);
    if (fd < 0) { printf("cannot open /dev/fb0\n"); return 1; }

    struct fb_var_screeninfo v; struct fb_fix_screeninfo f;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &v) < 0 || ioctl(fd, FBIOGET_FSCREENINFO, &f) < 0) {
        printf("ioctl failed\n"); return 1;
    }
    xres = v.xres; yres = v.yres; line_len = f.line_length; smem = f.smem_len;
    const size_t plane = (size_t)line_len * yres;
    printf("panel %ux%u line_length=%u smem_len=%u plane=%lu planes=%.2f\n",
           xres, yres, line_len, smem, (unsigned long)plane, (double)smem / (double)plane);
    printf("xoffset=%u yoffset=%u xres_virtual=%u yres_virtual=%u\n",
           v.xoffset, v.yoffset, v.xres_virtual, v.yres_virtual);
    fflush(stdout);

    fb = mmap(NULL, smem, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (fb == MAP_FAILED) { printf("mmap failed\n"); return 1; }

    phase(1, "plane 0 + UPDATE_DISPLAY",        0,     plane, trigger_ioctl);
    phase(2, "plane 1 + UPDATE_DISPLAY",        plane, plane, trigger_ioctl);
    phase(3, "whole mapping + UPDATE_DISPLAY",  0,     smem,  trigger_ioctl);
    phase(4, "whole mapping + /proc trigger",   0,     smem,  trigger_proc);
    phase(5, "whole mapping + UPDATE_AREA",     0,     smem,  trigger_area);

    fill_plane(0, smem, 0xF); trigger_ioctl();
    munmap(fb, smem); close(fd);
    printf("done\n");
    return 0;
}
