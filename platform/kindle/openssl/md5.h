#pragma once
//
// Minimal OpenSSL MD5 API for the Kindle cross build.
//
// vendor/simulator/src/MD5Builder_linux.h uses CommonCrypto on macOS and
// <openssl/md5.h> on Linux. The cross sysroot has no OpenSSL, and pulling the
// whole library in to compute one hash would be absurd - so this provides
// exactly the four symbols that header touches, backed by a real RFC 1321
// implementation (see md5_compat.c), not a stub.
//
// Deliberately NOT a general OpenSSL shim: it declares only MD5, so any other
// OpenSSL use in the tree still fails loudly at compile time rather than
// silently resolving to nothing.

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MD5_DIGEST_LENGTH 16

typedef struct MD5state_st {
  uint32_t state[4];   // A, B, C, D
  uint64_t bitcount;   // message length in bits
  uint8_t buffer[64];  // partial block
  uint32_t buflen;     // bytes currently in buffer
} MD5_CTX;

int MD5_Init(MD5_CTX* ctx);
int MD5_Update(MD5_CTX* ctx, const void* data, size_t len);
int MD5_Final(unsigned char* md, MD5_CTX* ctx);

#ifdef __cplusplus
}
#endif
