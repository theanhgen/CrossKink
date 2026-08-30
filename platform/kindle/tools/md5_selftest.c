/* RFC 1321 test vectors for md5_compat.c. Build and run natively. */
#include <stdio.h>
#include <string.h>
#include "openssl/md5.h"

static const struct { const char *in, *want; } V[] = {
  {"",                                                                 "d41d8cd98f00b204e9800998ecf8427e"},
  {"a",                                                                "0cc175b9c0f1b6a831c399e269772661"},
  {"abc",                                                              "900150983cd24fb0d6963f7d28e17f72"},
  {"message digest",                                                   "f96b697d7cb7938d525a2f31aaf161d0"},
  {"abcdefghijklmnopqrstuvwxyz",                                       "c3fcd3d76192e4007dfb496cca67e13b"},
  {"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789",   "d174ab98d277d9f5a5611c2c9f419d9f"},
  {"12345678901234567890123456789012345678901234567890123456789012345678901234567890",
                                                                       "57edf4a22be3c955ac49da2e2107b67a"},
};

int main(void) {
  int fail = 0;
  for (unsigned i = 0; i < sizeof V / sizeof V[0]; i++) {
    MD5_CTX c; unsigned char d[16]; char hex[33];
    MD5_Init(&c);
    MD5_Update(&c, V[i].in, strlen(V[i].in));
    MD5_Final(d, &c);
    for (int j = 0; j < 16; j++) sprintf(hex + j*2, "%02x", d[j]);
    int ok = strcmp(hex, V[i].want) == 0;
    if (!ok) fail = 1;
    printf("%s  %-32s %s\n", ok ? "PASS" : "FAIL", hex, ok ? "" : V[i].want);
  }
  /* streaming in odd chunks must match one-shot */
  { MD5_CTX c; unsigned char d[16]; char hex[33];
    const char *s = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    MD5_Init(&c);
    for (size_t i = 0; i < strlen(s); i += 7) {
      size_t n = strlen(s) - i; if (n > 7) n = 7;
      MD5_Update(&c, s + i, n);
    }
    MD5_Final(d, &c);
    for (int j = 0; j < 16; j++) sprintf(hex + j*2, "%02x", d[j]);
    int ok = strcmp(hex, "d174ab98d277d9f5a5611c2c9f419d9f") == 0;
    if (!ok) fail = 1;
    printf("%s  chunked streaming\n", ok ? "PASS" : "FAIL");
  }
  printf(fail ? "\nSELFTEST FAILED\n" : "\nALL VECTORS PASS\n");
  return fail;
}
