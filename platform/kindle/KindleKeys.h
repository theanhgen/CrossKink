#pragma once
#include <cstdint>

// Kindle 3 Keyboard (D00901, B00A) keycode map.
//
// MEASURED on-device 2026-08-29 with tools/keyguide.c, one key at a time with
// on-screen confirmation. Raw capture: gate/results/keymap-measured-2026-08-29.txt
//
//   DEVICE    CODE  KEY
//   mxckpd    193   left bar,  upper half
//   mxckpd    104   left bar,  lower half      (KEY_PAGEUP)
//   mxckpd    109   right bar, upper half      (KEY_PAGEDOWN)
//   mxckpd    191   right bar, lower half
//   fiveway   103   5-way up                   (KEY_UP)
//   fiveway   108   5-way down                 (KEY_DOWN)
//   fiveway   105   5-way left                 (KEY_LEFT)
//   fiveway   106   5-way right                (KEY_RIGHT)
//   fiveway   194   5-way centre
//   mxckpd    102   Home                       (KEY_HOME)
//   mxckpd    139   Menu                       (KEY_MENU)
//   mxckpd    158   Back                       (KEY_BACK)
//   volume    114   volume down                (KEY_VOLUMEDOWN)
//
// Note the split: the 5-way is on /dev/input/event1 ("fiveway"), everything
// else on event0 ("mxckpd"). The page bars are NOT on a device of their own.
//
// Raw numbers, deliberately not linux/input.h constants: two of these (191,
// 193, 194) have no KEY_* name that means anything here, and writing
// KEY_PAGEUP for 104 would imply a page semantic the hardware does not
// actually promise (see kPagePrev/kPageNext below).

namespace kindlekeys {

// --- page-turn bars (event0) ---
constexpr uint16_t kLeftBarUpper = 193;
constexpr uint16_t kLeftBarLower = 104;
constexpr uint16_t kRightBarUpper = 109;
constexpr uint16_t kRightBarLower = 191;

// Which bars mean "previous" and which mean "next".
//
// This is a CHOICE, not a measurement, and the hardware does not settle it:
// the two standard codes disagree with position. 104 (KEY_PAGEUP, i.e. back)
// is the LOWER left bar, while 109 (KEY_PAGEDOWN, i.e. forward) is the UPPER
// right bar. So keycode semantics and physical position point opposite ways,
// and the other two bars (191, 193) are vendor codes carrying no semantics.
//
// Resolved by position, so both sides behave identically: upper = previous,
// lower = next. Flip these two lists if it feels wrong in the hand.
constexpr uint16_t kPagePrevA = kLeftBarUpper;   // 193
constexpr uint16_t kPagePrevB = kRightBarUpper;  // 109
constexpr uint16_t kPageNextA = kLeftBarLower;   // 104
constexpr uint16_t kPageNextB = kRightBarLower;  // 191

// --- 5-way controller (event1) ---
constexpr uint16_t kFiveUp = 103;
constexpr uint16_t kFiveDown = 108;
constexpr uint16_t kFiveLeft = 105;
constexpr uint16_t kFiveRight = 106;
constexpr uint16_t kFiveSelect = 194;

// --- chrome keys (event0) ---
constexpr uint16_t kHome = 102;
constexpr uint16_t kMenu = 139;
constexpr uint16_t kBack = 158;

// --- volume rocker (event2) ---
constexpr uint16_t kVolumeDown = 114;
constexpr uint16_t kVolumeUp = 115;  // inferred: not captured, pairs with 114

}  // namespace kindlekeys
