// Host test: the fast 1bpp->4bpp path must be byte-identical to the reference.
#include "../KindleBlit.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static int failures = 0;

static void check(const char* name, bool ok) {
  std::printf("%-42s %s\n", name, ok ? "PASS" : "FAIL");
  if (!ok) failures++;
}

int main() {
  const int widthPixels = 600;
  const int widthBytes = widthPixels / 8;   // 75
  const int dstBytes = widthPixels / 2;     // 300

  // Measured panel palette: inverted, so a set bit (white) is nibble 0x0.
  const uint8_t white = 0x0, black = 0xF;

  std::srand(12345);
  bool allMatch = true;
  for (int trial = 0; trial < 400; ++trial) {
    std::vector<uint8_t> src(widthBytes);
    for (auto& b : src) b = static_cast<uint8_t>(std::rand() & 0xFF);

    std::vector<uint8_t> a(dstBytes, 0xAA), b2(dstBytes, 0x55);  // different fill
    kindleblit::packRowReference(src.data(), a.data(), widthPixels, white, black, true);
    kindleblit::packRowFast(src.data(), b2.data(), widthBytes, white, black);
    if (std::memcmp(a.data(), b2.data(), dstBytes) != 0) { allMatch = false; break; }
  }
  check("fast == reference over 400 random rows", allMatch);

  // Edge patterns that a shift/mask bug would survive random testing on.
  const uint8_t pats[][4] = {{0x00,0x00,0x00,0x00},{0xFF,0xFF,0xFF,0xFF},
                             {0x80,0x00,0x00,0x01},{0xAA,0x55,0xAA,0x55},
                             {0x01,0x80,0x0F,0xF0}};
  bool edgeOk = true;
  for (auto& p : pats) {
    std::vector<uint8_t> src(widthBytes);
    for (int i = 0; i < widthBytes; ++i) src[i] = p[i % 4];
    std::vector<uint8_t> a(dstBytes, 0), b2(dstBytes, 0);
    kindleblit::packRowReference(src.data(), a.data(), widthPixels, white, black, true);
    kindleblit::packRowFast(src.data(), b2.data(), widthBytes, white, black);
    if (std::memcmp(a.data(), b2.data(), dstBytes) != 0) edgeOk = false;
  }
  check("fast == reference on edge bit patterns", edgeOk);

  // Pixel 0 must land in the HIGH nibble of byte 0 (the unverifiable-on-panel
  // constant): a lone leading white pixel on a black row.
  {
    std::vector<uint8_t> src(widthBytes, 0x00);
    src[0] = 0x80;  // only pixel 0 set
    std::vector<uint8_t> d(dstBytes, 0);
    kindleblit::packRowFast(src.data(), d.data(), widthBytes, white, black);
    check("pixel 0 -> high nibble of byte 0",
          ((d[0] >> 4) & 0xF) == white && (d[0] & 0xF) == black);
  }

  // Polarity actually applied: an all-set row must be entirely 'white'.
  {
    std::vector<uint8_t> src(widthBytes, 0xFF);
    std::vector<uint8_t> d(dstBytes, 0xCC);
    kindleblit::packRowFast(src.data(), d.data(), widthBytes, white, black);
    bool ok = true;
    for (int i = 0; i < dstBytes; ++i) ok = ok && d[i] == ((white << 4) | white);
    check("all-set row -> all white nibbles", ok);
  }

  std::printf("\n%s\n", failures ? "BLIT TESTS FAILED" : "all blit tests pass");
  return failures ? 1 : 0;
}
