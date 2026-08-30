#pragma once
#include <cstdint>

// Kindle 3 Keyboard (D00901) panel geometry.
//
// Measured on-device 2026-08-28 (gate/results/recon-result-2026-08-28.txt):
//   /dev/fb0  id='eink_fb'  xres=600 yres=800 rotate=0 grayscale=1
//
// Portrait, unlike upstream's landscape X3 (792x528) / X4 (800x480).
// 600 is divisible by 8, so DISPLAY_WIDTH_BYTES is exact: 75 * 800 = 60000 B.
//
// This header shadows the simulator's EInkDisplay.h; platform/kindle must come
// first on the include path.

#ifndef EPD_SCLK
#define EPD_SCLK 0
#endif
#ifndef EPD_MOSI
#define EPD_MOSI 0
#endif
#ifndef EPD_CS
#define EPD_CS 0
#endif
#ifndef EPD_DC
#define EPD_DC 0
#endif
#ifndef EPD_RST
#define EPD_RST 0
#endif
#ifndef EPD_BUSY
#define EPD_BUSY 0
#endif

class EInkDisplay {
 public:
  static constexpr uint16_t DISPLAY_WIDTH = 600;
  static constexpr uint16_t DISPLAY_HEIGHT = 800;

  enum RefreshMode { FULL_REFRESH, HALF_REFRESH, FAST_REFRESH };

  // The Kindle kernel driver owns the panel: waveforms, temperature
  // compensation and the controller handshake all live behind einkfb. Every
  // member here is therefore a no-op, exactly as in the simulator stub. The
  // real work is in HalDisplay.cpp, which talks to /dev/fb0 directly.
  EInkDisplay() = default;
  EInkDisplay(int, int, int, int, int, int) {}
  void begin() {}
  void clearScreen(uint8_t) {}
  void drawImage(const uint8_t*, uint16_t, uint16_t, uint16_t, uint16_t, bool = false) {}
  void drawImageTransparent(const uint8_t*, uint16_t, uint16_t, uint16_t, uint16_t, bool = false) {}
  void displayBuffer(RefreshMode, bool) {}
  void refreshDisplay(RefreshMode, bool) {}
  void deepSleep() {}
  uint8_t* getFrameBuffer() {
    static uint8_t buf[DISPLAY_WIDTH * DISPLAY_HEIGHT / 8];
    return buf;
  }
  void copyGrayscaleBuffers(const uint8_t*, const uint8_t*) {}
  void copyGrayscaleLsbBuffers(const uint8_t*) {}
  void copyGrayscaleMsbBuffers(const uint8_t*) {}
  void cleanupGrayscaleBuffers(const uint8_t*) {}
  void displayGrayBuffer(bool = false, const unsigned char* = nullptr, bool = false) {}
};
