#pragma once
#include <cstddef>
#include <cstdint>
//
// 1bpp -> 4bpp packing, kept free of <linux/fb.h> and the HAL so it can be
// unit-tested on the host. HalDisplay.cpp is the only production caller;
// tools/blit_test.cpp exercises it directly.
//
// CrossInk stores 1 bit per pixel, MSB = leftmost, 8 pixels per byte. The
// panel wants 4 bits per pixel, 2 pixels per byte. A set bit means WHITE
// (matching getBit() semantics in HalDisplay.cpp).

namespace kindleblit {

// Reference implementation: one pixel at a time. Obvious, slow, and the
// oracle the fast path is tested against.
inline void packRowReference(const uint8_t* src, uint8_t* dst, int widthPixels, uint8_t white,
                             uint8_t black, bool highNibbleIsLeftPixel) {
  for (int x = 0; x < widthPixels; ++x) {
    const bool set = (src[x / 8] & (1u << (7 - (x % 8)))) != 0;
    const uint8_t nib = set ? white : black;
    uint8_t& b = dst[x / 2];
    const bool highHalf = highNibbleIsLeftPixel ? ((x % 2) == 0) : ((x % 2) == 1);
    if (highHalf) {
      b = static_cast<uint8_t>((b & 0x0F) | (nib << 4));
    } else {
      b = static_cast<uint8_t>((b & 0xF0) | nib);
    }
  }
}

// Fast path: one source byte (8 px) becomes four destination bytes via a
// 4-entry lookup, with no per-pixel arithmetic. Only valid for the common
// layout where the high nibble holds the left pixel.
inline void packRowFast(const uint8_t* src, uint8_t* dst, int widthBytes, uint8_t white,
                        uint8_t black) {
  const uint8_t ww = static_cast<uint8_t>((white << 4) | white);
  const uint8_t wb = static_cast<uint8_t>((white << 4) | black);
  const uint8_t bw = static_cast<uint8_t>((black << 4) | white);
  const uint8_t bb = static_cast<uint8_t>((black << 4) | black);
  const uint8_t lut[4] = {bb, bw, wb, ww};  // index = two source bits

  for (int b = 0; b < widthBytes; ++b) {
    const uint8_t v = src[b];
    dst[0] = lut[(v >> 6) & 0x3];
    dst[1] = lut[(v >> 4) & 0x3];
    dst[2] = lut[(v >> 2) & 0x3];
    dst[3] = lut[v & 0x3];
    dst += 4;
  }
}

}  // namespace kindleblit
