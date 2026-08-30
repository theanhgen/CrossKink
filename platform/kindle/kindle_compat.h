#pragma once
//
// Force-included into every translation unit (-include). Fills the gaps
// between the macOS/BSD libc the simulator was written against and the
// glibc 2.5 on this device.
//
// Keep this C-compatible: it is force-included into the C sources too
// (expat, miniz, uzlib, minibidi, QRCode).

#include <stddef.h>
#include <string.h>

// strlcpy/strlcat are BSD. macOS has had them forever; glibc only added them
// in 2.38, and this device runs 2.5. The semantics below are the BSD ones:
// always NUL-terminate (given size > 0) and return the length of src, so the
// caller can detect truncation.
#if !defined(__APPLE__) && !defined(__BSD_VISIBLE)
#if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 38))
// glibc is new enough to provide them.
#else

#ifdef __cplusplus
extern "C" {
#endif

static inline size_t kindle_strlcpy(char* dst, const char* src, size_t size) {
  const size_t srclen = strlen(src);
  if (size != 0) {
    const size_t n = (srclen < size - 1) ? srclen : size - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
  }
  return srclen;
}

// Bounded strlen, spelled out rather than calling strnlen: strnlen is POSIX,
// not ISO C, so it is not declared under -std=c11, and GCC 14 treats the
// resulting implicit declaration as a hard error.
static inline size_t kindle_bounded_len(const char* s, size_t maxlen) {
  size_t n = 0;
  while (n < maxlen && s[n] != '\0') n++;
  return n;
}

static inline size_t kindle_strlcat(char* dst, const char* src, size_t size) {
  const size_t dstlen = kindle_bounded_len(dst, size);
  const size_t srclen = strlen(src);
  if (dstlen == size) return size + srclen;  // dst not NUL-terminated
  const size_t space = size - dstlen - 1;
  const size_t n = (srclen < space) ? srclen : space;
  memcpy(dst + dstlen, src, n);
  dst[dstlen + n] = '\0';
  return dstlen + srclen;
}

#ifdef __cplusplus
}
#endif

#define strlcpy kindle_strlcpy
#define strlcat kindle_strlcat

#endif
#endif
