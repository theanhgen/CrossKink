// Kindle entry point. Replaces crossink-simulator/src/simulator_main.cpp.
//
// Two differences from the simulator's loop:
//
//  1. No SDL. SDL_Delay becomes usleep, SDL_Quit disappears.
//  2. The Kindle framework has to be stopped while we own the display and the
//     input devices, and put back afterwards - including if we crash or are
//     killed. That is what the signal handling below is for: leaving a user's
//     Kindle with a suspended framework and no reader is the worst thing this
//     program could do.
//
// The pause/resume sequence is KOReader's, which is the closest thing to a
// convention on this platform: SIGSTOP cvm on entry; SIGCONT plus two synthetic
// KEY_MENU presses through /proc/keypad on exit, which is what makes the stock
// UI repaint instead of coming back to a screen we drew on.

#include <GfxRenderer.h>
#include <unistd.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#include "Arduino.h"
#include "HalDisplay.h"
#include "HalGPIO.h"
#include "KindleQuit.h"
#include "SimulatorLifecycle.h"

extern void setup();
extern void loop();
extern HalDisplay display;
extern GfxRenderer renderer;  // defined in crossink/src/main.cpp

namespace {

volatile sig_atomic_t g_stop = 0;
bool g_frameworkPaused = false;

void pauseFramework() {
  if (g_frameworkPaused) return;
  // Ignore the return value: on a device where cvm is not running (a stripped
  // image, or a second instance) there is simply nothing to stop.
  (void)std::system("killall -STOP cvm 2>/dev/null");
  g_frameworkPaused = true;
}

// Must stay async-signal-safe enough to survive being reached from a handler.
// system() is not strictly signal-safe, but the alternative - leaving the
// framework stopped - is far worse than the small risk here.
void resumeFramework() {
  if (!g_frameworkPaused) return;
  g_frameworkPaused = false;
  (void)std::system("killall -CONT cvm 2>/dev/null");
  // Two menu presses: the first wakes the framework's input path, the second
  // makes it redraw. One is not reliably enough.
  (void)std::system("echo 'send 139' > /proc/keypad 2>/dev/null");
  (void)std::system("echo 'send 139' > /proc/keypad 2>/dev/null");
}

void onSignal(int) { g_stop = 1; }

// CROSSINK_RUN_SECONDS bounds the session. Set it when launching from an OTA
// update script: that script is sourced by the updater and must return, so an
// interactive reader running forever would hang the update (and needs a
// 30-second power-slider reset to recover). Unset or 0 = run until told to
// stop, which is what a normal launch wants.
long runSecondsLimit() {
  const char* s = std::getenv("CROSSINK_RUN_SECONDS");
  if (s == nullptr || *s == '\0') return 0;
  const long v = std::strtol(s, nullptr, 10);
  return (v > 0) ? v : 0;
}

void installHandlers() {
  struct sigaction sa{};
  sa.sa_handler = onSignal;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGINT, &sa, nullptr);
  sigaction(SIGTERM, &sa, nullptr);
  sigaction(SIGHUP, &sa, nullptr);
  // A broken pipe on the web server must not take the reader down.
  signal(SIGPIPE, SIG_IGN);
}

}  // namespace

int main(int /*argc*/, char** argv) {
  SimulatorLifecycle::initProcessArgs(argv);

  installHandlers();
  // Also cover the paths that do not come through a signal.
  std::atexit(resumeFramework);

  pauseFramework();

  // Panel orientation.
  //
  // GfxRenderer defaults to Orientation::Portrait (GfxRenderer.h:132), which
  // rotates logical coordinates 90 degrees clockwise - that is how upstream
  // gets a portrait UI out of a natively LANDSCAPE 800x480 panel.
  //
  // The Kindle 3 panel is natively PORTRAIT (600x800), so applying that
  // rotation renders the UI sideways - which is exactly what the first
  // on-device run showed. What we want is the identity mapping
  // (phyX = x, phyY = y), which despite its name is
  // LandscapeCounterClockwise ("aligned with panel orientation",
  // GfxRenderer.cpp:392).
  //
  // Themes flip to Portrait temporarily for rotated text and restore the
  // previous value, so setting this once here holds.
  renderer.setOrientation(GfxRenderer::Orientation::LandscapeCounterClockwise);

  setup();

  const long limit = runSecondsLimit();
  const time_t started = time(nullptr);

  while (!g_stop && !display.shouldQuit() && !kindle::quitRequested()) {
    if (limit > 0 && (time(nullptr) - started) >= limit) {
      std::fprintf(stderr, "[kindle] CROSSINK_RUN_SECONDS=%ld reached, exiting\n", limit);
      break;
    }
    // Clear input edge latches once per frame. update() may be called many
    // times within loop(); edges must survive across those calls and reset
    // only here, at the frame boundary.
    gpio.beginFrame();
    loop();
    // ~1 kHz ceiling, matching the simulator. The real limit is e-ink refresh
    // time, not this.
    usleep(1000);
  }

  if (kindle::quitRequested()) {
    std::fprintf(stderr, "[kindle] Menu held - exiting cleanly\n");
  }

  resumeFramework();

  // Same reasoning as the simulator: bypass global destructors rather than
  // race the render thread on the way out. resumeFramework() has already run,
  // so nothing is left suspended.
  std::fflush(nullptr);
  _exit(0);
}
