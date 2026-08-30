/* keydump - report every key event from the Kindle's input devices.
 *
 * Ground truth for KindleKeys.h. Run on-device, press every button, then read
 * the output. Read-only: opens the event nodes O_RDONLY and prints. */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <stdlib.h>
#include <time.h>

#define MAXDEV 4

int main(int argc, char **argv) {
    struct pollfd pfd[MAXDEV];
    char names[MAXDEV][256];
    int n = 0;
    int seconds = (argc > 1) ? atoi(argv[1]) : 30;

    for (int i = 0; i < MAXDEV && n < MAXDEV; i++) {
        char path[32];
        snprintf(path, sizeof path, "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY);
        if (fd < 0) continue;
        memset(names[n], 0, sizeof names[n]);
        if (ioctl(fd, EVIOCGNAME(sizeof names[n] - 1), names[n]) < 0)
            strcpy(names[n], "(unknown)");
        printf("watching %s = \"%s\"\n", path, names[n]);
        pfd[n].fd = fd;
        pfd[n].events = POLLIN;
        n++;
    }
    if (n == 0) { printf("no input devices\n"); return 1; }

    printf("\npress every key now (%d s)\n", seconds);
    printf("%-10s %-6s %-6s %s\n", "DEVICE", "CODE", "VALUE", "ACTION");
    fflush(stdout);

    time_t end = time(NULL) + seconds;
    while (time(NULL) < end) {
        int r = poll(pfd, n, 1000);
        if (r <= 0) continue;
        for (int i = 0; i < n; i++) {
            if (!(pfd[i].revents & POLLIN)) continue;
            struct input_event ev;
            while (read(pfd[i].fd, &ev, sizeof ev) == sizeof ev) {
                if (ev.type != EV_KEY) continue;
                const char *action = ev.value == 1 ? "press"
                                   : ev.value == 0 ? "release" : "repeat";
                printf("%-10s %-6u %-6d %s\n", names[i], ev.code, ev.value, action);
                fflush(stdout);
            }
        }
    }
    printf("\ndone\n");
    for (int i = 0; i < n; i++) close(pfd[i].fd);
    return 0;
}
