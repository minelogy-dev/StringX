// SPDX-License-Identifier: MIT
#define _GNU_SOURCE
#include "recheck.h"
#include <string.h>

#ifdef RECHECK_VERIFY_CALLS
#define RECORD() (++recheck_call_counts[LIB_INDEX])
#else
#define RECORD() ((void)0)
#endif

#if LIB_INDEX == 0
__attribute__((noinline)) uint64_t recheck_sx(recheck_input *f) {
  RECORD();
  size_t p = f->m == 1 ? sx_find_char(f->sx, (char)f->pattern[0])
                       : sx_find(f->sx, f->sx_pattern);
  return p == (size_t)-1 ? 0 : (uint64_t)p + 1;
}
#elif LIB_INDEX == 1
__attribute__((noinline)) uint64_t recheck_sds(recheck_input *f) {
  RECORD();
  const char *p = f->m == 1 ? memchr(f->sd, f->pattern[0], sdslen(f->sd))
                            : memmem(f->sd, sdslen(f->sd), f->pattern, f->m);
  return p ? (uint64_t)(p - f->sd) + 1 : 0;
}
#elif LIB_INDEX == 2
__attribute__((noinline)) uint64_t recheck_bstr(recheck_input *f) {
  RECORD();
  int p = f->m == 1 ? bstrchr(f->bs, f->pattern[0])
                    : binstr(f->bs, 0, f->bs_pattern);
  return p < 0 ? 0 : (uint64_t)p + 1;
}
#elif LIB_INDEX == 3
__attribute__((noinline)) uint64_t recheck_raw(recheck_input *f) {
  RECORD();
  /* Same SDS buffer and needle as the SDS adapter; explicit length
     isolates header/adapter overhead from the glibc implementation. */
  const char *p = f->m == 1 ? memchr(f->sd, f->pattern[0], f->n)
                            : memmem(f->sd, f->n, f->pattern, f->m);
  return p ? (uint64_t)(p - f->sd) + 1 : 0;
}
#else
#error LIB_INDEX must be 0..3
#endif
