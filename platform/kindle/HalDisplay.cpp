// HalDisplay - Kindle 3 Keyboard backend.
//
// Replaces the simulator's SDL window/texture with the legacy einkfb
// framebuffer. Substitution is link-time: this file must expose exactly the
// symbols declared in the HalDisplay.h that ships with the simulator tree.
//
// Hardware facts, measured on-device 2026-08-28
// (gate/results/recon-result-2026-08-28.txt):
//
//   /dev/fb0   id='eink_fb'  600x800  bits_per_pixel=4  line_length=300
//              grayscale=1   rotate=0  smem_len=483328
//
// CrossInk renders 1bpp (60000 B); the panel wants 4bpp packed, 2 px/byte
// (240000 B visible). The conversion is the whole job of this file. Note this
// is NOT the simulator's unpack path: that one emits ARGB32 for a texture.
// Only the bit-extraction half (getBit) carries over.

#include "HalDisplay.h"

#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

namespace {

// ---------------------------------------------------------------------------
// einkfb ABI
//
// Legacy einkfb, as used by K2/K3/DXG. Values cross-checked against
// koreader-base ffi/einkfb_h.lua; see PROJECT.md 5.4.
// ---------------------------------------------------------------------------

constexpr unsigned long FBIO_EINK_UPDATE_DISPLAY = 0x46DB;       // 18139
constexpr unsigned long FBIO_EINK_UPDATE_DISPLAY_AREA = 0x46DD;  // 18141

enum FxType : int {
  FX_UPDATE_PARTIAL = 0,  // differential, no flash - the page-turn workhorse
  FX_UPDATE_FULL = 1,     // full flashing refresh, clears ghosting
  FX_UPDATE_SLOW = 3,     // slower waveform, better contrast
};

struct UpdateArea {
  int x1;
  int y1;
  int x2;
  int y2;
  FxType which_fx;
  unsigned char* buffer;  // null = use the mapped framebuffer
};

// ---------------------------------------------------------------------------
// Panel state
// ---------------------------------------------------------------------------

int fbFd = -1;
uint8_t* fbMem = nullptr;
size_t fbMemLen = 0;
uint32_t fbLineLength = 0;
uint16_t fbXres = 0;
uint16_t fbYres = 0;
bool fbReady = false;

// CrossInk's 1bpp framebuffer. Statically allocated so lendFrameBufferStorage()
// can hand out its storage without the allocation ever moving (see the contract
// in HalDisplay.h).
std::array<uint8_t, HalDisplay::BUFFER_SIZE> frameBufferStorage{};
bool frameBufferLent = false;

// Grayscale planes. CrossInk composes 4 levels from a BW base plus LSB/MSB
// bit planes; the panel gives us 16, so all four land exactly.
struct GrayscaleState {
  std::array<uint8_t, HalDisplay::BUFFER_SIZE> bwBase{};
  std::array<uint8_t, HalDisplay::BUFFER_SIZE> lsbPlane{};
  std::array<uint8_t, HalDisplay::BUFFER_SIZE> msbPlane{};
  bool bwBaseValid = false;
  bool lsbValid = false;
  bool msbValid = false;
};
GrayscaleState grayscale;

// 4bpp nibble levels, MEASURED from the panel 2026-08-29 by sampling
// /dev/fb0 while the stock Kindle UI was on screen
// (gate/results/fbsample-polarity-2026-08-29.txt):
//
//   0x0  93.43%   <- background, i.e. WHITE
//   0x5   1.07%
//   0xA   0.59%
//   0xF   4.91%   <- ink, i.e. BLACK
//
// Two things fall out of that histogram. The panel is INVERTED relative to
// what fb_var_screeninfo's grayscale=1 implies: low nibble is bright, not
// dark. And the stock UI uses exactly four evenly spaced levels and nothing
// else - which happens to match CrossInk's own 4-level model, so the values
// below are the panel's native palette rather than anything invented.
//
// Expressed panel-native, so no separate inversion step is needed.
constexpr uint8_t kNibWhite = 0x0;
constexpr uint8_t kNibLight = 0x5;
constexpr uint8_t kNibDark = 0xA;
constexpr uint8_t kNibBlack = 0xF;

// Nibble order within a byte: leftmost pixel in the high nibble.
//
// VERIFIED 2026-08-30 by rendered text. No test pattern can settle this - a
// swap maps a pattern P(x) to P(x^1), and everything coarse enough to see on
// e-ink is symmetric under that - but a wrong value serrates glyphs at
// single-pixel granularity. Text renders clean on device, so this is correct.
constexpr bool kHighNibbleIsLeftPixel = true;

// Panel polarity is already baked into the kNib* constants above, so the only
// flip left is display.isInverted() - the user-facing black/white swap. The
// palette is symmetric (0x0<->0xF, 0x5<->0xA), so 0xF - nib inverts it exactly.
inline uint8_t applyPolarity(uint8_t nib, bool userInverted) {
  return userInverted ? static_cast<uint8_t>(0xF - nib) : nib;
}

// Carried over from crossink-simulator/src/HalDisplay.cpp - the one piece of
// that file that is genuinely reusable.
inline bool getBit(const uint8_t* buffer, int x, int y) {
  const int byteIdx = (y * HalDisplay::DISPLAY_WIDTH + x) / 8;
  const int bitIdx = 7 - (x % 8);
  return (buffer[byteIdx] & (1 << bitIdx)) != 0;
}

inline void putNibblePair(uint8_t* row, int xPair, uint8_t leftNib, uint8_t rightNib) {
  row[xPair] = kHighNibbleIsLeftPixel
                   ? static_cast<uint8_t>((leftNib << 4) | rightNib)
                   : static_cast<uint8_t>((rightNib << 4) | leftNib);
}

// Resolve the 4bpp level for one pixel from the BW base plus optional planes.
inline uint8_t levelAt(const uint8_t* bw, int x, int y, bool useGray) {
  if (!getBit(bw, x, y)) {
    if (useGray) {
      const bool lsb = grayscale.lsbValid && getBit(grayscale.lsbPlane.data(), x, y);
      const bool msb = grayscale.msbValid && getBit(grayscale.msbPlane.data(), x, y);
      if (msb) return lsb ? kNibDark : kNibLight;
      if (lsb) return kNibDark;
    }
    return kNibBlack;
  }
  return kNibWhite;
}

// The hot path: 1bpp (or 1bpp + 2 planes) -> 4bpp packed, straight into the
// mapped framebuffer. Walks rows so it honours line_length rather than
// assuming width/2 stride.
void blitToPanel(const uint8_t* bw, bool useGray) {
  if (!fbReady || bw == nullptr) return;

  const int width = std::min<int>(HalDisplay::DISPLAY_WIDTH, fbXres);
  const int height = std::min<int>(HalDisplay::DISPLAY_HEIGHT, fbYres);

  const bool inv = display.isInverted();

  // Fast path: plain black-and-white, which is almost every frame.
  //
  // The generic path below calls levelAt() per pixel, and that recomputes
  // (y * WIDTH + x) / 8 every time - a multiply and a divide for each of
  // 480,000 pixels, on a 532 MHz ARM11. Here one source byte (8 pixels)
  // becomes four destination bytes with no per-pixel arithmetic at all.
  if (!useGray && width == HalDisplay::DISPLAY_WIDTH && kHighNibbleIsLeftPixel) {
    const uint8_t white = applyPolarity(kNibWhite, inv);
    const uint8_t black = applyPolarity(kNibBlack, inv);
    // Byte pair for two same-colour pixels, indexed by bit.
    const uint8_t ww = static_cast<uint8_t>((white << 4) | white);
    const uint8_t wb = static_cast<uint8_t>((white << 4) | black);
    const uint8_t bw_ = static_cast<uint8_t>((black << 4) | white);
    const uint8_t bb = static_cast<uint8_t>((black << 4) | black);
    const uint8_t lut[4] = {bb, bw_, wb, ww};  // index = (hi bit, lo bit)

    for (int y = 0; y < height; ++y) {
      const uint8_t* src = bw + (static_cast<size_t>(y) * HalDisplay::DISPLAY_WIDTH_BYTES);
      uint8_t* dst = fbMem + (static_cast<size_t>(y) * fbLineLength);
      for (int b = 0; b < HalDisplay::DISPLAY_WIDTH_BYTES; ++b) {
        const uint8_t v = src[b];
        // Bit set = white in CrossInk's 1bpp buffer (getBit semantics).
        dst[0] = lut[(v >> 6) & 0x3];
        dst[1] = lut[(v >> 4) & 0x3];
        dst[2] = lut[(v >> 2) & 0x3];
        dst[3] = lut[v & 0x3];
        dst += 4;
      }
    }
    return;
  }

  for (int y = 0; y < height; ++y) {
    uint8_t* row = fbMem + (static_cast<size_t>(y) * fbLineLength);
    for (int x = 0; x + 1 < width; x += 2) {
      const uint8_t l = applyPolarity(levelAt(bw, x, y, useGray), inv);
      const uint8_t r = applyPolarity(levelAt(bw, x + 1, y, useGray), inv);
      putNibblePair(row, x / 2, l, r);
    }
    if (width & 1) {  // odd width: pair the last pixel with white
      const uint8_t l = applyPolarity(levelAt(bw, width - 1, y, useGray), inv);
      putNibblePair(row, (width - 1) / 2, l, applyPolarity(kNibWhite, inv));
    }
  }
}

FxType fxFor(HalDisplay::RefreshMode mode) {
  switch (mode) {
    case HalDisplay::FULL_REFRESH:
      return FX_UPDATE_FULL;
    case HalDisplay::HALF_REFRESH:
      // This is what CrossInk asks for when its page counter decides it is
      // time to clear ghosting, so it has to actually flash. FX_UPDATE_SLOW is
      // a gentler non-flashing waveform and left the residue behind - which is
      // why ghosting persisted before.
      return FX_UPDATE_FULL;
    case HalDisplay::FAST_REFRESH:
    default:
      return FX_UPDATE_PARTIAL;
  }
}

// Ghost management.
//
// CrossInk already owns this policy: SETTINGS.getRefreshFrequency() gives
// "pages between full refreshes" (1/5/10/15/30, user-settable in Settings),
// and ReaderActivity::initialRefreshCountdown() drives it. That counts PAGES,
// which is the right unit and how every other reader does it - stock Kindle
// historically flashed every 6 pages, KOReader defaults to 10.
//
// This counter must therefore stay well out of the way. It counts ioctls, not
// pages, and a single page turn issues several (body, footer, progress), so a
// low value here fires far more often than the user asked for and fights the
// app's own policy. It exists purely as a backstop for UI screens that sit
// outside the reader's page accounting.
constexpr unsigned kPartialsBeforeFull = 60;
unsigned partialsSinceFull = 0;

void panelUpdate(FxType fx) {
  if (!fbReady) return;

  if (fx == FX_UPDATE_PARTIAL) {
    if (++partialsSinceFull >= kPartialsBeforeFull) {
      fx = FX_UPDATE_FULL;
      partialsSinceFull = 0;
    }
  } else {
    // A full or slow waveform clears the accumulated residue itself.
    partialsSinceFull = 0;
  }

  int arg = static_cast<int>(fx);
  if (ioctl(fbFd, FBIO_EINK_UPDATE_DISPLAY, arg) < 0) {
    std::fprintf(stderr, "[HalDisplay] FBIO_EINK_UPDATE_DISPLAY(%d) failed\n", arg);
  }
}

void panelUpdateArea(int x, int y, int w, int h, FxType fx) {
  if (!fbReady) return;
  UpdateArea area{};
  area.x1 = std::max(0, x);
  area.y1 = std::max(0, y);
  area.x2 = std::min<int>(fbXres, x + w);
  area.y2 = std::min<int>(fbYres, y + h);
  area.which_fx = fx;
  area.buffer = nullptr;
  if (area.x2 <= area.x1 || area.y2 <= area.y1) return;
  if (ioctl(fbFd, FBIO_EINK_UPDATE_DISPLAY_AREA, &area) < 0) {
    // Not fatal: fall back to a whole-screen update.
    panelUpdate(fx);
  }
}

void clearGrayscalePlanes() {
  grayscale.lsbPlane.fill(0);
  grayscale.msbPlane.fill(0);
  grayscale.lsbValid = false;
  grayscale.msbValid = false;
}

void copyPlane(std::array<uint8_t, HalDisplay::BUFFER_SIZE>& dst, const uint8_t* src, bool& valid) {
  if (src == nullptr) {
    valid = false;
    dst.fill(0);
    return;
  }
  std::memcpy(dst.data(), src, HalDisplay::BUFFER_SIZE);
  valid = true;
}

}  // namespace

HalDisplay display;

HalDisplay::HalDisplay() = default;

HalDisplay::~HalDisplay() {
  if (fbMem != nullptr && fbMem != MAP_FAILED) munmap(fbMem, fbMemLen);
  if (fbFd >= 0) close(fbFd);
  fbMem = nullptr;
  fbFd = -1;
  fbReady = false;
}

void HalDisplay::begin() { begin(false); }

void HalDisplay::begin(bool /*seamless*/) {
  if (fbReady) return;

  fbFd = open("/dev/fb0", O_RDWR);
  if (fbFd < 0) {
    std::fprintf(stderr, "[HalDisplay] cannot open /dev/fb0\n");
    return;
  }

  struct fb_var_screeninfo var {};
  struct fb_fix_screeninfo fix {};
  if (ioctl(fbFd, FBIOGET_VSCREENINFO, &var) < 0 ||
      ioctl(fbFd, FBIOGET_FSCREENINFO, &fix) < 0) {
    std::fprintf(stderr, "[HalDisplay] framebuffer ioctl failed\n");
    close(fbFd);
    fbFd = -1;
    return;
  }

  // Refuse to guess. The packing below is 4bpp-specific; anything else would
  // silently paint garbage.
  if (var.bits_per_pixel != 4) {
    std::fprintf(stderr, "[HalDisplay] expected 4bpp, got %ubpp - refusing to blit\n",
                 var.bits_per_pixel);
    close(fbFd);
    fbFd = -1;
    return;
  }

  fbXres = static_cast<uint16_t>(var.xres);
  fbYres = static_cast<uint16_t>(var.yres);
  fbLineLength = fix.line_length;
  fbMemLen = fix.smem_len;

  fbMem = static_cast<uint8_t*>(mmap(nullptr, fbMemLen, PROT_READ | PROT_WRITE, MAP_SHARED, fbFd, 0));
  if (fbMem == MAP_FAILED) {
    std::fprintf(stderr, "[HalDisplay] mmap of %zu bytes failed\n", fbMemLen);
    fbMem = nullptr;
    close(fbFd);
    fbFd = -1;
    return;
  }

  fbReady = true;

  if (fbXres != DISPLAY_WIDTH || fbYres != DISPLAY_HEIGHT) {
    std::fprintf(stderr, "[HalDisplay] panel is %ux%u but build expects %ux%u\n", fbXres, fbYres,
                 DISPLAY_WIDTH, DISPLAY_HEIGHT);
  }

  clearScreen(0xFF);
  displayBuffer(FULL_REFRESH, false);
}

void HalDisplay::clearScreen(uint8_t color) const {
  frameBufferStorage.fill(color);
}

void HalDisplay::drawImage(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                           bool /*fromProgmem*/) const {
  if (imageData == nullptr) return;
  uint8_t* fb = frameBufferStorage.data();
  const uint16_t srcStride = static_cast<uint16_t>((w + 7) / 8);
  for (uint16_t row = 0; row < h; ++row) {
    const int py = y + row;
    if (py < 0 || py >= DISPLAY_HEIGHT) continue;
    for (uint16_t col = 0; col < w; ++col) {
      const int px = x + col;
      if (px < 0 || px >= DISPLAY_WIDTH) continue;
      const bool on = (imageData[row * srcStride + (col / 8)] & (1 << (7 - (col % 8)))) != 0;
      const int byteIdx = (py * DISPLAY_WIDTH + px) / 8;
      const int bitIdx = 7 - (px % 8);
      if (on)
        fb[byteIdx] |= static_cast<uint8_t>(1 << bitIdx);
      else
        fb[byteIdx] &= static_cast<uint8_t>(~(1 << bitIdx));
    }
  }
}

void HalDisplay::drawImageTransparent(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w,
                                      uint16_t h, bool /*fromProgmem*/) const {
  if (imageData == nullptr) return;
  uint8_t* fb = frameBufferStorage.data();
  const uint16_t srcStride = static_cast<uint16_t>((w + 7) / 8);
  for (uint16_t row = 0; row < h; ++row) {
    const int py = y + row;
    if (py < 0 || py >= DISPLAY_HEIGHT) continue;
    for (uint16_t col = 0; col < w; ++col) {
      const int px = x + col;
      if (px < 0 || px >= DISPLAY_WIDTH) continue;
      // Transparent variant: only clear (draw black), never set white.
      const bool on = (imageData[row * srcStride + (col / 8)] & (1 << (7 - (col % 8)))) != 0;
      if (on) continue;
      const int byteIdx = (py * DISPLAY_WIDTH + px) / 8;
      fb[byteIdx] &= static_cast<uint8_t>(~(1 << (7 - (px % 8))));
    }
  }
}

void HalDisplay::displayBuffer(RefreshMode mode, bool /*turnOffScreen*/) {
  blitToPanel(frameBufferStorage.data(), false);
  panelUpdate(fxFor(mode));
}

// The kernel driver owns the waveform and returns once it has queued the
// update, so there is no userspace-visible deferral to exploit. Async
// degrades to a blocking refresh, and supportsAsyncRefresh() says so.
void HalDisplay::displayBufferAsync(RefreshMode mode) { displayBuffer(mode, false); }

void HalDisplay::waitRefreshComplete() {}

bool HalDisplay::supportsAsyncRefresh() const { return false; }

bool HalDisplay::supportsAsyncGrayscaleBase() const { return false; }

void HalDisplay::refreshDisplay(RefreshMode mode, bool /*turnOffScreen*/) {
  panelUpdate(fxFor(mode));
}

void HalDisplay::deepSleep() {
  // Panel power is the kernel's business; nothing to do here.
}

uint8_t* HalDisplay::getFrameBuffer() const {
  return frameBufferLent ? nullptr : const_cast<uint8_t*>(frameBufferStorage.data());
}

uint8_t* HalDisplay::lendFrameBufferStorage(uint32_t* sizeOut) {
  if (frameBufferLent) return nullptr;
  frameBufferLent = true;
  if (sizeOut != nullptr) *sizeOut = BUFFER_SIZE;
  return frameBufferStorage.data();
}

void HalDisplay::returnFrameBufferStorage() {
  if (!frameBufferLent) return;
  frameBufferLent = false;
  frameBufferStorage.fill(0xFF);  // contract: comes back white, caller redraws
}

// --- grayscale ------------------------------------------------------------
//
// Upstream's X3/X4 waveform choreography (precondition, deferred base, strip
// streaming) exists to drive the panel controller directly. Here the kernel
// does that, so the sequence collapses to: remember the planes, then compose
// all four levels in one blit.

void HalDisplay::displayGrayscaleBase(RefreshMode fallback, bool /*turnOffScreen*/) {
  std::memcpy(grayscale.bwBase.data(), frameBufferStorage.data(), BUFFER_SIZE);
  grayscale.bwBaseValid = true;
  clearGrayscalePlanes();
  blitToPanel(grayscale.bwBase.data(), false);
  panelUpdate(fxFor(fallback));
}

void HalDisplay::preconditionGrayscale() {}
void HalDisplay::preconditionGrayscale(uint16_t, uint16_t, uint16_t, uint16_t) {}

void HalDisplay::copyGrayscaleBuffers(const uint8_t* lsbBuffer, const uint8_t* msbBuffer) {
  copyPlane(grayscale.lsbPlane, lsbBuffer, grayscale.lsbValid);
  copyPlane(grayscale.msbPlane, msbBuffer, grayscale.msbValid);
}

void HalDisplay::copyGrayscaleLsbBuffers(const uint8_t* lsbBuffer) {
  copyPlane(grayscale.lsbPlane, lsbBuffer, grayscale.lsbValid);
}

void HalDisplay::copyGrayscaleMsbBuffers(const uint8_t* msbBuffer) {
  copyPlane(grayscale.msbPlane, msbBuffer, grayscale.msbValid);
}

void HalDisplay::cleanupGrayscaleBuffers(const uint8_t* bwBuffer) {
  if (bwBuffer != nullptr) {
    std::memcpy(grayscale.bwBase.data(), bwBuffer, BUFFER_SIZE);
    grayscale.bwBaseValid = true;
  }
  clearGrayscalePlanes();
}

void HalDisplay::displayGrayBuffer(bool /*turnOffScreen*/, const unsigned char* /*lut*/,
                                   bool /*factoryMode*/) {
  const uint8_t* base =
      grayscale.bwBaseValid ? grayscale.bwBase.data() : frameBufferStorage.data();
  blitToPanel(base, true);
  // Grayscale needs the slower waveform; a partial update smears the levels.
  panelUpdate(FX_UPDATE_SLOW);
}

void HalDisplay::writeGrayscalePlaneStrip(bool lsbPlane, const uint8_t* rows, uint16_t yStart,
                                          uint16_t numRows) {
  if (rows == nullptr) return;
  auto& dst = lsbPlane ? grayscale.lsbPlane : grayscale.msbPlane;
  bool& valid = lsbPlane ? grayscale.lsbValid : grayscale.msbValid;

  const size_t stride = DISPLAY_WIDTH_BYTES;
  const size_t offset = static_cast<size_t>(yStart) * stride;
  size_t bytes = static_cast<size_t>(numRows) * stride;
  if (offset >= dst.size()) return;
  bytes = std::min(bytes, dst.size() - offset);

  std::memcpy(dst.data() + offset, rows, bytes);
  valid = true;
}

bool HalDisplay::supportsStripGrayscale() const { return true; }

uint16_t HalDisplay::getDisplayWidth() const { return DISPLAY_WIDTH; }
uint16_t HalDisplay::getDisplayHeight() const { return DISPLAY_HEIGHT; }
uint16_t HalDisplay::getDisplayWidthBytes() const { return DISPLAY_WIDTH_BYTES; }
uint32_t HalDisplay::getBufferSize() const { return BUFFER_SIZE; }

// --- simulator-header surface -------------------------------------------
//
// The forked simulator tree's HalDisplay.h is a superset of CrossInk's
// lib/hal one. These exist so the fork links; most are genuinely nothing on
// this platform.

void HalDisplay::setInverted(bool value) {
  if (inverted == value) return;
  inverted = value;
  blitToPanel(frameBufferStorage.data(), false);
  panelUpdate(FX_UPDATE_FULL);  // a polarity swap ghosts badly on a partial
}

bool HalDisplay::toggleInverted() {
  setInverted(!inverted);
  return inverted;
}

bool HalDisplay::isInverted() const { return inverted; }

// Damage-rect update. The framebuffer already holds current content, so this
// only needs to ask the panel to refresh that rectangle.
void HalDisplay::displayWindow(int x, int y, int w, int h) {
  blitToPanel(frameBufferStorage.data(), false);
  panelUpdateArea(x, y, w, h, FX_UPDATE_PARTIAL);
}

void HalDisplay::displayFactoryGrayBuffer(bool turnOffScreen) {
  displayGrayBuffer(turnOffScreen, nullptr, true);
}

// Simulator-only concepts: there is no host window to pump and no quit event.
void HalDisplay::presentIfNeeded() {}
bool HalDisplay::shouldQuit() const { return false; }
