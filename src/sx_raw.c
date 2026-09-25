#include "sx_raw.h"
#include "_sx.h"
#include "export.h"
#include <string.h>

/* allocates `cap` bytes for ptr; keeps ptr[len] == '\0' (len starts 0) */
SYMBOL_PUBLIC _sx *sx__new(size_t cap) {
  _sx *s = malloc(sizeof(_sx));
  if (s == NULL)
    return NULL;
  size_t n = cap ? cap : 1; /* >= 1 byte so ptr[0] == '\0' has a home */
  s->ptr = malloc(n);
  if (s->ptr == NULL) {
    free(s);
    return NULL;
  }
  s->cap = n;
  s->len = 0;
  s->ptr[0] = '\0';
  return s;
}
SYMBOL_PUBLIC _sx *sx__dup(const _sx *src) {
  if (!src)
    return NULL;
  _sx *s = sx__new(src->len + 1); /* content + one terminator byte */
  if (s == NULL)
    return NULL;
  memcpy(s->ptr, src->ptr, src->len);
  s->len = src->len;
  s->ptr[s->len] = '\0';
  return s;
}
/* Grown with two policies, selected at compile time:
   - default: geometric growth (double, or +0x10000 once past 64 KiB)
     — amortizes realloc; leaves slack bytes beyond len.
   - -DSX_FORCE_EXACT_RESERVE: exact sizing, cap == need + 1 for
     every growth (and sx__new is already exact).  The allocation
     then ends exactly at the terminator: any access beyond len is
     an ASan redzone hit immediately, so off-by-one bugs and stale
     lens cannot hide behind slack capacity.  Costs a copy per grow;
     meant for debugging builds. */
/* ensure cap > need: room for `need` content bytes plus the terminator
   (the len <= cap-1 invariant).  Callers pass the content length they
   must be able to hold. */
SYMBOL_PUBLIC int sx__reserve(_sx *s, size_t need) {
  size_t n;
  if (!s)
    return 1;
  if (s->cap > need)
    return 0;
  if (need == (size_t)-1)
    return 1;
#if defined(SX_FORCE_EXACT_RESERVE)
  n = need + 1;
#else
  if (s->cap & (size_t)~0xffff)
    n = s->cap + 0x10000;
  else
    n = s->cap << 1;
  n = n > need + 1 ? n : need + 1;
#endif
  char *ptr = realloc(s->ptr, n * sizeof(char));
  if (ptr == NULL)
    return 1;
  s->ptr = ptr;
  s->cap = n;
  return 0;
}
SYMBOL_PUBLIC void sx__free(_sx *s) {
  if (!s)
    return;
  free(s->ptr);
  free(s);
}
