#pragma once
//
// Clean exit path for the Kindle build.
//
// The Kindle has no shell and no KUAL, so CrossInk is launched from an OTA
// update package - and that script is sourced by the updater, which must
// return. A reader with no way to quit therefore hangs the update and needs a
// 30-second power-slider reset to recover (learned the hard way).
//
// The Menu key (keycode 139) is not bound to any CrossInk button, so it is
// free to serve as the exit. A HOLD rather than a tap, so it cannot be hit by
// accident mid-page.
//
// Declared here rather than as a HalGPIO method because HalGPIO.h is vendored
// from the simulator and its public API must stay byte-for-byte identical for
// link-time substitution to work.

namespace kindle {

// True once the Menu key has been held for the exit duration.
bool quitRequested();

// Milliseconds the Menu key must be held.
constexpr unsigned long kQuitHoldMs = 2000;

}  // namespace kindle
