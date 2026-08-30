// RFC 1321 MD5, for the Kindle cross build. See openssl/md5.h for why.
//
// Verified against the RFC 1321 test vectors by tools/md5_selftest.c.

#include "openssl/md5.h"

#include <string.h>

#define ROTL(x, c) (((x) << (c)) | ((x) >> (32 - (c))))

static const uint32_t K[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a,
    0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
    0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340,
    0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8,
    0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
    0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa,
    0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92,
    0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
    0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};

static const uint8_t R[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7,
                              12, 17, 22, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,
                              14, 20, 5, 9,  14, 20, 4, 11, 16, 23, 4, 11, 16,
                              23, 4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15, 21,
                              6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

static void md5_block(uint32_t state[4], const uint8_t block[64]) {
  uint32_t M[16];
  for (int i = 0; i < 16; i++) {
    // MD5 is little-endian; read byte-wise so this is endian-independent.
    M[i] = (uint32_t)block[i * 4] | ((uint32_t)block[i * 4 + 1] << 8) |
           ((uint32_t)block[i * 4 + 2] << 16) | ((uint32_t)block[i * 4 + 3] << 24);
  }

  uint32_t a = state[0], b = state[1], c = state[2], d = state[3];

  for (int i = 0; i < 64; i++) {
    uint32_t f;
    int g;
    if (i < 16) {
      f = (b & c) | (~b & d);
      g = i;
    } else if (i < 32) {
      f = (d & b) | (~d & c);
      g = (5 * i + 1) % 16;
    } else if (i < 48) {
      f = b ^ c ^ d;
      g = (3 * i + 5) % 16;
    } else {
      f = c ^ (b | ~d);
      g = (7 * i) % 16;
    }
    const uint32_t tmp = d;
    d = c;
    c = b;
    b = b + ROTL(a + f + K[i] + M[g], R[i]);
    a = tmp;
  }

  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
}

int MD5_Init(MD5_CTX* ctx) {
  if (!ctx) return 0;
  ctx->state[0] = 0x67452301;
  ctx->state[1] = 0xefcdab89;
  ctx->state[2] = 0x98badcfe;
  ctx->state[3] = 0x10325476;
  ctx->bitcount = 0;
  ctx->buflen = 0;
  memset(ctx->buffer, 0, sizeof ctx->buffer);
  return 1;
}

int MD5_Update(MD5_CTX* ctx, const void* data, size_t len) {
  if (!ctx || (!data && len)) return 0;
  const uint8_t* p = (const uint8_t*)data;
  ctx->bitcount += (uint64_t)len * 8u;

  if (ctx->buflen) {
    const size_t need = 64u - ctx->buflen;
    const size_t take = (len < need) ? len : need;
    memcpy(ctx->buffer + ctx->buflen, p, take);
    ctx->buflen += (uint32_t)take;
    p += take;
    len -= take;
    if (ctx->buflen == 64u) {
      md5_block(ctx->state, ctx->buffer);
      ctx->buflen = 0;
    }
  }

  while (len >= 64u) {
    md5_block(ctx->state, p);
    p += 64;
    len -= 64;
  }

  if (len) {
    memcpy(ctx->buffer, p, len);
    ctx->buflen = (uint32_t)len;
  }
  return 1;
}

int MD5_Final(unsigned char* md, MD5_CTX* ctx) {
  if (!md || !ctx) return 0;
  const uint64_t bits = ctx->bitcount;

  static const uint8_t pad[64] = {0x80};
  const size_t padlen = (ctx->buflen < 56u) ? (56u - ctx->buflen) : (120u - ctx->buflen);
  MD5_Update(ctx, pad, padlen);
  ctx->bitcount = bits;  // Update() advanced it; the length field must not count padding

  uint8_t lenbuf[8];
  for (int i = 0; i < 8; i++) lenbuf[i] = (uint8_t)((bits >> (8 * i)) & 0xFF);
  MD5_Update(ctx, lenbuf, 8);

  for (int i = 0; i < 4; i++) {
    md[i * 4 + 0] = (uint8_t)(ctx->state[i] & 0xFF);
    md[i * 4 + 1] = (uint8_t)((ctx->state[i] >> 8) & 0xFF);
    md[i * 4 + 2] = (uint8_t)((ctx->state[i] >> 16) & 0xFF);
    md[i * 4 + 3] = (uint8_t)((ctx->state[i] >> 24) & 0xFF);
  }
  return 1;
}
