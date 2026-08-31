/* keyguide - guided, one-key-at-a-time keycode capture.
 *
 * The free-form keydump recorded which codes exist but not which physical key
 * produced each one. This prompts for a single named key, waits for exactly
 * one press, records device+code, and advances. Unambiguous by construction.
 *
 * Read-only on input: event nodes are opened O_RDONLY. Prompts go through
 * eips, which paints the e-ink screen (the reader repaints over it later). */
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define MAXDEV 4
#define TIMEOUT_S 90

/* Trimmed to what HalGPIO actually needs. Volume is already known
 * (volume/114) and Aa/Sym are not bound to anything. */
static const char* PROMPTS[] = {
    "LEFT bar  - UPPER half",
    "LEFT bar  - LOWER half",
    "RIGHT bar - UPPER half",
    "RIGHT bar - LOWER half",
    "5-way  UP",
    "5-way  DOWN",
    "5-way  LEFT",
    "5-way  RIGHT",
    "5-way  CENTRE (press in)",
    "HOME",
    "MENU",
    "BACK",
};
#define NPROMPTS ((int)(sizeof(PROMPTS) / sizeof(PROMPTS[0])))

static void eips(int row, const char* text) {
  char cmd[512];
  snprintf(cmd, sizeof cmd, "eips 0 %d \"%s\" 2>/dev/null", row, text);
  system(cmd);
}

int main(void) {
  struct pollfd pfd[MAXDEV];
  char names[MAXDEV][128];
  int n = 0;

  for (int i = 0; i < MAXDEV && n < MAXDEV; i++) {
    char path[32];
    snprintf(path, sizeof path, "/dev/input/event%d", i);
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) continue;
    memset(names[n], 0, sizeof names[n]);
    if (ioctl(fd, EVIOCGNAME(sizeof names[n] - 1), names[n]) < 0) strcpy(names[n], "?");
    printf("device event%d = \"%s\"\n", i, names[n]);
    pfd[n].fd = fd;
    pfd[n].events = POLLIN;
    n++;
  }
  if (n == 0) {
    printf("no input devices\n");
    return 1;
  }

  printf("\n%-26s %-10s %-6s\n", "KEY", "DEVICE", "CODE");
  fflush(stdout);

  eips(3, "  CrossInk guided keycode capture         ");

  for (int p = 0; p < NPROMPTS; p++) {
    char line[128];
    snprintf(line, sizeof line, "  [%d/%d] press:  %-24s", p + 1, NPROMPTS, PROMPTS[p]);
    eips(6, line);
    eips(8, "  wait for 'got:' before next press      ");

    /* Drain anything already queued so a previous release cannot satisfy
     * this prompt. */
    for (int i = 0; i < n; i++) {
      struct input_event junk;
      while (read(pfd[i].fd, &junk, sizeof junk) == sizeof junk) {
      }
    }

    int got = 0;
    time_t end = time(NULL) + TIMEOUT_S;
    while (!got && time(NULL) < end) {
      for (int i = 0; i < n; i++) {
        pfd[i].events = POLLIN;
        pfd[i].revents = 0;
      }
      if (poll(pfd, n, 500) <= 0) continue;
      for (int i = 0; i < n && !got; i++) {
        if (!(pfd[i].revents & POLLIN)) continue;
        struct input_event ev;
        while (read(pfd[i].fd, &ev, sizeof ev) == sizeof ev) {
          if (ev.type == EV_KEY && ev.value == 1) {
            printf("%-26s %-10s %-6u\n", PROMPTS[p], names[i], ev.code);
            fflush(stdout);
            /* Echo it back immediately. Without this there is no
             * way to tell the prompt advanced, which is exactly
             * how the previous run drifted out of sync. */
            char ack[128];
            snprintf(ack, sizeof ack, "  got: %s code %-4u  -- next in 2s   ", names[i], ev.code);
            eips(10, ack);
            got = 1;
            break;
          }
        }
      }
    }
    if (!got) {
      printf("%-26s %-10s %-6s\n", PROMPTS[p], "-", "NONE");
      fflush(stdout);
      eips(10, "  nothing pressed - moving on          ");
    }
    /* Long enough to read the acknowledgement and let go of the key. */
    sleep(2);
    eips(10, "                                        ");
  }

  eips(6, "  CAPTURE FINISHED - rebooting shortly    ");
  eips(8, "                                          ");
  printf("\ndone\n");
  for (int i = 0; i < n; i++) close(pfd[i].fd);
  return 0;
}
