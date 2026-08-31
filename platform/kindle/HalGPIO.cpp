// HalGPIO - Kindle 3 Keyboard backend.
//
// Replaces the simulator's SDL_PollEvent with Linux evdev. Substitution is
// link-time: this must expose exactly the symbols the forked simulator's
// HalGPIO.h declares.
//
// Input devices, measured on-device 2026-08-28
// (gate/results/recon-result-2026-08-28.txt):
//
//   /dev/input/event0  "mxckpd"   QWERTY keypad AND the page-turn bars
//   /dev/input/event1  "fiveway"  5-way controller
//   /dev/input/event2  "volume"   volume rocker
//
// All three are EV_KEY only - there is no touch, and no separate page-bar
// device. Keycode values live in KindleKeys.h and are still unverified;
// see the note there.

#include "HalGPIO.h"

#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

#include "KindleKeys.h"
#include "KindleQuit.h"

// linux/input.h defines BTN_LEFT (0x110), BTN_RIGHT (0x111) and BTN_BACK
// (0x116) as macros for mouse/gamepad buttons. They collide with HalGPIO's
// button-index constants of the same name, so `HalGPIO::BTN_LEFT` expands to
// `HalGPIO::0x110` and fails to parse. Drop the macros: this file wants the
// class constants, and it refers to keyboard keys as KEY_*, never BTN_*.
#undef BTN_LEFT
#undef BTN_RIGHT
#undef BTN_BACK

namespace {

constexpr int kMaxDevices = 3;
constexpr uint8_t kButtonCount = 7;

int devFds[kMaxDevices];
int devCount = 0;

bool pressed[kButtonCount];
bool pressEdge[kButtonCount];
bool releaseEdge[kButtonCount];
unsigned long pressedAtMs[kButtonCount];
unsigned long lastHeldMs = 0;

bool usbConnected = false;
bool usbChanged = false;

// Exit gesture: Menu held. See KindleQuit.h for why this exists and why it is
// a hold rather than a tap.
bool quitFlag = false;
bool menuDown = false;
unsigned long menuDownAt = 0;

// Map a Linux keycode onto a CrossInk button index, or 0xFF for "not ours".
uint8_t buttonForKey(uint16_t code) {
  using namespace kindlekeys;
  switch (code) {
    // Page-turn bars. Both edges carry a pair.
    //
    // These belong on UP/DOWN, not LEFT/RIGHT. CrossInk's page-turn channel is
    // its *side* buttons, and kSideLayouts (MappedInputManager.cpp) wires
    // Button::PageBack/PageForward to BTN_UP/BTN_DOWN -- on the Xteink the side
    // rocker is physically vertical. Putting the bars on LEFT/RIGHT still
    // turned pages, because the reader treats the front Left/Right as prev/next
    // too, but it routed them down the front-button path: it doubled the bars
    // onto the same two buttons as the 5-way's horizontal axis, and picked up
    // the front long-press action (chapter skip) instead of the side one.
    case kPagePrevA:
    case kPagePrevB:
      return HalGPIO::BTN_UP;
    case kPageNextA:
    case kPageNextB:
      return HalGPIO::BTN_DOWN;

    case kFiveLeft:
      return HalGPIO::BTN_LEFT;
    case kFiveRight:
      return HalGPIO::BTN_RIGHT;
    case kFiveUp:
      return HalGPIO::BTN_UP;
    case kFiveDown:
      return HalGPIO::BTN_DOWN;
    case kFiveSelect:
      return HalGPIO::BTN_CONFIRM;

    case kBack:
      return HalGPIO::BTN_BACK;

    default:
      return 0xFF;
  }
}

void openDevices() {
  if (devCount > 0) return;
  for (int i = 0; i < kMaxDevices; ++i) {
    char path[32];
    std::snprintf(path, sizeof path, "/dev/input/event%d", i);
    const int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) continue;
    devFds[devCount++] = fd;
  }
  if (devCount == 0) std::fprintf(stderr, "[HalGPIO] no /dev/input/event* nodes\n");
}

// The K3 exposes USB state through the gadget driver's sysfs entry rather
// than an input event.
bool readUsbConnected() {
  const char* candidates[] = {
      "/sys/devices/platform/fsl-usb2-udc/gadget/suspended",
      "/sys/class/power_supply/usb/online",
  };
  for (const char* path : candidates) {
    const int fd = open(path, O_RDONLY);
    if (fd < 0) continue;
    char buf[8] = {0};
    const ssize_t n = read(fd, buf, sizeof buf - 1);
    close(fd);
    if (n <= 0) continue;
    // "suspended" is inverted: 1 means the bus is idle, i.e. not connected.
    const bool value = (buf[0] == '1');
    return (std::strstr(path, "suspended") != nullptr) ? !value : value;
  }
  return false;
}

}  // namespace

HalGPIO gpio;

void HalGPIO::begin() {
  std::memset(pressed, 0, sizeof pressed);
  std::memset(pressEdge, 0, sizeof pressEdge);
  std::memset(releaseEdge, 0, sizeof releaseEdge);
  std::memset(pressedAtMs, 0, sizeof pressedAtMs);
  openDevices();
  usbConnected = readUsbConnected();
  usbChanged = false;
}

void HalGPIO::beginFrame() {
  std::memset(pressEdge, 0, sizeof pressEdge);
  std::memset(releaseEdge, 0, sizeof releaseEdge);
  usbChanged = false;
}

void HalGPIO::update() {
  if (devCount == 0) return;

  struct pollfd pfd[kMaxDevices];
  for (int i = 0; i < devCount; ++i) {
    pfd[i].fd = devFds[i];
    pfd[i].events = POLLIN;
    pfd[i].revents = 0;
  }

  // Non-blocking: the caller owns frame pacing.
  if (poll(pfd, devCount, 0) <= 0) {
    // Still refresh held time so getHeldTime() advances while a key is down,
    // and let a Menu hold reach the quit threshold with no new events.
    for (uint8_t b = 0; b < kButtonCount; ++b) {
      if (pressed[b]) lastHeldMs = millis() - pressedAtMs[b];
    }
    if (menuDown && (millis() - menuDownAt) >= kindle::kQuitHoldMs) {
      quitFlag = true;
    }
    return;
  }

  for (int i = 0; i < devCount; ++i) {
    if (!(pfd[i].revents & POLLIN)) continue;
    struct input_event ev;
    while (read(devFds[i], &ev, sizeof ev) == static_cast<ssize_t>(sizeof ev)) {
      if (ev.type != EV_KEY) continue;
      if (ev.value == 2) continue;  // autorepeat: edges only

      // Menu is not mapped to a CrossInk button, so it is free to be the exit
      // gesture. Handled here on the raw keycode, before button mapping.
      if (ev.code == kindlekeys::kMenu) {
        if (ev.value == 1) {
          menuDown = true;
          menuDownAt = millis();
        } else {
          menuDown = false;
        }
        continue;
      }

      const uint8_t btn = buttonForKey(ev.code);
      if (btn == 0xFF) continue;

      if (ev.value == 1) {
        if (!pressed[btn]) {
          pressed[btn] = true;
          pressEdge[btn] = true;
          pressedAtMs[btn] = millis();
        }
      } else {
        if (pressed[btn]) {
          pressed[btn] = false;
          releaseEdge[btn] = true;
          lastHeldMs = millis() - pressedAtMs[btn];
        }
      }
    }
  }

  if (menuDown && (millis() - menuDownAt) >= kindle::kQuitHoldMs) {
    quitFlag = true;
  }

  const bool nowUsb = readUsbConnected();
  if (nowUsb != usbConnected) {
    usbConnected = nowUsb;
    usbChanged = true;
  }
}

bool HalGPIO::isPressed(uint8_t buttonIndex) const { return buttonIndex < kButtonCount && pressed[buttonIndex]; }

bool HalGPIO::wasPressed(uint8_t buttonIndex) const { return buttonIndex < kButtonCount && pressEdge[buttonIndex]; }

bool HalGPIO::wasReleased(uint8_t buttonIndex) const { return buttonIndex < kButtonCount && releaseEdge[buttonIndex]; }

bool HalGPIO::wasAnyPressed() const {
  for (uint8_t b = 0; b < kButtonCount; ++b)
    if (pressEdge[b]) return true;
  return false;
}

bool HalGPIO::wasAnyReleased() const {
  for (uint8_t b = 0; b < kButtonCount; ++b)
    if (releaseEdge[b]) return true;
  return false;
}

unsigned long HalGPIO::getHeldTime() const { return lastHeldMs; }

// The K3's power control is a sliding switch wired to the PMIC, not an evdev
// key: userspace never sees a press, so there is no hold duration to report.
unsigned long HalGPIO::getPowerButtonHeldTime() const { return 0; }

bool HalGPIO::isXteinkDevice() const { return false; }

// The page-turn bars sit on the left and right screen edges, which is exactly
// what this flag describes.
bool HalGPIO::hasEdgeSideButtons() const { return true; }

bool HalGPIO::hasTouch() const { return false; }
bool HalGPIO::supportsMultiTouch() const { return false; }
bool HalGPIO::hasHomeKey() const { return false; }
bool HalGPIO::wasHomeKeyPressed() const { return false; }
bool HalGPIO::wasHomeKeyTapped() const { return false; }
bool HalGPIO::wasHomeKeyLongPressed() const { return false; }
bool HalGPIO::wasTouchTap(float&, float&) const { return false; }
bool HalGPIO::wasTouchDown(float&, float&) const { return false; }
bool HalGPIO::wasTouchReleased() const { return false; }
bool HalGPIO::isTouchTapCandidate(float&, float&, unsigned long&) const { return false; }
bool HalGPIO::wasTouchLongPress(float&, float&) const { return false; }
void HalGPIO::suppressTouchContact() {}
bool HalGPIO::isTouchHeldAt(float&, float&) const { return false; }
unsigned long HalGPIO::lastTouchHeldMs() const { return 0; }
bool HalGPIO::wasSwipe(float&, float&, float&, float&) const { return false; }
bool HalGPIO::wasTouchActivity() const { return false; }

void HalGPIO::setSharedConfirmPowerShortPressEmitsPower(bool) {}

bool HalGPIO::consumeSimulatorSleepRequest() { return false; }

// Sleep is the Kindle framework's job; a hosted process must not put the
// device to sleep behind its back.
void HalGPIO::startDeepSleep() {}

bool HalGPIO::verifyPowerButtonWakeup(uint16_t, bool) { return true; }

bool HalGPIO::isUsbConnected() const { return usbConnected; }
bool HalGPIO::wasUsbStateChanged() const { return usbChanged; }

HalGPIO::WakeupReason HalGPIO::getWakeupReason() const { return WakeupReason::Other; }

namespace kindle {
bool quitRequested() { return quitFlag; }
}  // namespace kindle
