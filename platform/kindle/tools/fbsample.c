/* fbsample - determine panel polarity by reading, not by looking.
 *
 * Asking a human "is the top third black?" needs them watching at the right
 * moment. This avoids the question entirely: the Kindle's own UI is dark text
 * on a light background, so whatever nibble value DOMINATES the framebuffer is
 * white. That fixes polarity outright.
 *
 *   dominant nibble 0xF -> higher value = brighter -> kInvertNibbles = false
 *   dominant nibble 0x0 -> inverted panel          -> kInvertNibbles = true
 *
 * Also reports the full 16-bucket histogram, which shows how many grey levels
 * the stock UI actually uses - a direct answer to how much tonal range the
 * port could exploit.
 *
 * READ-ONLY: opens /dev/fb0 O_RDONLY, maps PROT_READ, writes nothing. */
#include <fcntl.h>
#include <linux/fb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

int main(void) {
  int fd = open("/dev/fb0", O_RDONLY);
  if (fd < 0) {
    printf("cannot open /dev/fb0\n");
    return 1;
  }

  struct fb_var_screeninfo v;
  struct fb_fix_screeninfo f;
  if (ioctl(fd, FBIOGET_VSCREENINFO, &v) < 0 || ioctl(fd, FBIOGET_FSCREENINFO, &f) < 0) {
    printf("ioctl failed\n");
    return 1;
  }
  const unsigned xres = v.xres, yres = v.yres, ll = f.line_length;
  printf("panel %ux%u bpp=%u line_length=%u\n", xres, yres, v.bits_per_pixel, ll);

  const unsigned char* fb = mmap(NULL, f.smem_len, PROT_READ, MAP_SHARED, fd, 0);
  if (fb == MAP_FAILED) {
    printf("mmap failed\n");
    return 1;
  }

  unsigned long hist[16];
  memset(hist, 0, sizeof hist);
  unsigned long total = 0;

  for (unsigned y = 0; y < yres; y++) {
    const unsigned char* row = fb + (size_t)y * ll;
    for (unsigned b = 0; b < xres / 2; b++) {
      hist[(row[b] >> 4) & 0x0F]++;
      hist[row[b] & 0x0F]++;
      total += 2;
    }
  }

  printf("\nnibble histogram over %lu pixels:\n", total);
  int dom = 0;
  unsigned levels_used = 0;
  for (int i = 0; i < 16; i++) {
    if (hist[i] > hist[dom]) dom = i;
    if (hist[i] * 1000 / (total ? total : 1) > 0) levels_used++;
    printf("  0x%X  %10lu  %5.2f%%\n", i, hist[i], 100.0 * (double)hist[i] / (double)total);
  }

  printf("\ndominant nibble: 0x%X  (%.2f%% of screen)\n", dom, 100.0 * (double)hist[dom] / (double)total);
  printf("levels above 0.1%% of screen: %u\n", levels_used);

  if (dom >= 0x8)
    printf("VERDICT: background is a HIGH nibble -> higher = brighter -> kInvertNibbles = false (as shipped)\n");
  else
    printf("VERDICT: background is a LOW nibble -> panel is INVERTED -> set kInvertNibbles = true\n");

  munmap((void*)fb, f.smem_len);
  close(fd);
  return 0;
}
