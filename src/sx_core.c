#include "sx_core.h"
#include "_sx.h"
#include "export.h"
#include "sx_raw.h"
#include <stdint.h>
#include <string.h>

SYMBOL_PUBLIC sx_t *sx_new(void) { return sx_new_with_cap(32); }
SYMBOL_PUBLIC sx_t *sx_new_from_cstr(const char *s) {
  if (!s)
    return NULL;
  size_t l = strlen(s);
  sx_t *sx = sx_new_with_cap(l + 1); /* content + terminator */
  if (!sx)
    return NULL;
  memcpy(sx->ref->ptr, s, l);
  sx->ref->len = l;
  sx->ref->ptr[l] = '\0';
  return sx;
}
SYMBOL_PUBLIC sx_t *sx_new_from_view(sx_view_t v) {
  sx_t *s = sx_new_with_cap(v.len + 1); /* content + terminator */
  if (!s)
    return NULL;
  memcpy(s->ref->ptr, v.ref->ptr + v.offset, v.len);
  s->ref->len = v.len;
  s->ref->ptr[v.len] = '\0';
  return s;
}
SYMBOL_PUBLIC sx_t *sx_new_with_cap(size_t n) {
  sx_t *s = malloc(sizeof(sx_t));
  _sx *ptr = sx__new(n);
  if (!s || !ptr)
    goto cleanup;
  s->ref = ptr;
  return s;
cleanup:
  free(s);
  sx__free(ptr);
  return NULL;
}
SYMBOL_PUBLIC sx_t *sx_dup(const sx_t *src) {
  if (!src)
    return NULL;
  sx_t *sx = malloc(sizeof(sx_t));
  if (!sx)
    return NULL;
  sx->ref = sx__new(src->ref->len + 1); /* content + terminator */
  if (!sx->ref) {
    free(sx);
    return NULL;
  }
  memcpy(sx->ref->ptr, src->ref->ptr, src->ref->len);
  sx->ref->len = src->ref->len;
  sx->ref->ptr[sx->ref->len] = '\0';
  return sx;
}
SYMBOL_PUBLIC void sx_free(sx_t *h) {
  sx__free(h->ref);
  free(h);
}

SYMBOL_PUBLIC int sx_len(sx_t *h) { return h->ref->len; }
SYMBOL_PUBLIC int sx_len_view(sx_view_t v) { return v.len; }
SYMBOL_PUBLIC int sx_cap(sx_t *h) { return h->ref->cap; }
SYMBOL_PUBLIC char *sx_buf_mut(sx_t *h) { return h->ref->ptr; }
SYMBOL_PUBLIC char *sx_buf_view_mut(sx_view_t v) {
  return v.ref->ptr + v.offset;
}

SYMBOL_PUBLIC sx_view_t sx_view(const sx_t *h) {
  return (sx_view_t){.ref = h->ref, .len = h->ref->len, .offset = 0};
}
static inline sx_t *write(sx_t *self, size_t idx, const char *str, size_t len,
                          void *(func)(void *, const void *, size_t));
SYMBOL_PUBLIC sx_t *sx_append(sx_t *self, sx_t *owned) {
  if (!owned)
    return NULL;
  sx_t *ret =
      write(self, self->ref->len, owned->ref->ptr, owned->ref->len, memcpy);
  sx_free(owned);
  return ret;
}
SYMBOL_PUBLIC sx_t *sx_append_cstr(sx_t *self, const char *s) {
  if (!s)
    return NULL;
  return sx_write_cstr(self, self->ref->len, s);
}
SYMBOL_PUBLIC sx_t *sx_append_view(sx_t *self, sx_view_t v) {
  return sx_write_view(self, self->ref->len, v);
}
static inline sx_t *write(sx_t *self, size_t idx, const char *str, size_t len,
                          void *(*copy)(void *, const void *, size_t)) {
  if (sx__reserve(self->ref, idx + len))
    return NULL;
  copy(self->ref->ptr + idx, str, len * sizeof(char));
  if (idx + len > self->ref->len)
    self->ref->len = idx + len;
  self->ref->ptr[self->ref->len] = '\0'; /* keep the invariant */
  return self;
}
SYMBOL_PUBLIC sx_t *sx_write(sx_t *self, size_t idx, sx_t *owned) {
  if (!owned)
    return NULL;
  sx_t *ret = write(self, idx, owned->ref->ptr, owned->ref->len, memcpy);
  sx_free(owned);
  return ret;
}
SYMBOL_PUBLIC sx_t *sx_write_cstr(sx_t *self, size_t idx, const char *s) {
  if (!s)
    return NULL;
  return write(self, idx, s, strlen(s), memcpy);
}
SYMBOL_PUBLIC sx_t *sx_write_view(sx_t *self, size_t idx, sx_view_t v) {
  if (sx__reserve(self->ref, idx + v.len))
    return NULL;
  if (self->ref == v.ref)
    return write(self, idx, v.ref->ptr + v.offset, v.len, memmove);
  return write(self, idx, v.ref->ptr + v.offset, v.len, memcpy);
}
static inline sx_t *insert(sx_t *self, size_t idx, const char *str, size_t len,
                           void *(*copy)(void *, const void *, size_t)) {
  if (sx__reserve(self->ref, self->ref->len + len))
    return NULL;
  if (!write(self, idx + len, self->ref->ptr + idx, self->ref->len - idx,
             memmove))
    return NULL;
  return write(self, idx, str, len, copy);
}
SYMBOL_PUBLIC sx_t *sx_insert(sx_t *self, size_t idx, sx_t *owned) {
  if (!owned)
    return NULL;
  sx_t *ret = insert(self, idx, owned->ref->ptr, owned->ref->len, memcpy);
  sx_free(owned);
  return ret;
}
SYMBOL_PUBLIC sx_t *sx_insert_cstr(sx_t *self, size_t idx, const char *s) {
  if (!s)
    return NULL;
  return insert(self, idx, s, strlen(s), memcpy);
}
SYMBOL_PUBLIC sx_t *sx_insert_view(sx_t *self, size_t idx, sx_view_t v) {
  if (sx__reserve(self->ref, self->ref->len + v.len))
    return NULL;
  if (self->ref == v.ref)
    return insert(self, idx, v.ref->ptr + v.offset, v.len, memmove);
  return insert(self, idx, v.ref->ptr + v.offset, v.len, memcpy);
}
SYMBOL_PUBLIC sx_t *sx_truncate(sx_t *self, size_t new_len) {
  if (new_len > self->ref->len) {
    if (sx__reserve(self->ref, new_len))
      return NULL;
  }
  self->ref->len = new_len;
  self->ref->ptr[new_len] = '\0';
  return self;
}
SYMBOL_PUBLIC sx_t *sx_erase(sx_t *self, size_t idx, size_t n) {
  if (idx + n > self->ref->len)
    return self;
  if (!write(self, idx, self->ref->ptr + idx + n, self->ref->len - idx - n,
             memmove))
    return NULL;
  self->ref->len -= n;
  self->ref->ptr[self->ref->len] = '\0'; /* keep the invariant */
  return self;
}
SYMBOL_PUBLIC sx_t *sx_clear(sx_t *self) {
  self->ref->ptr[0] = '\0';
  self->ref->len = 0;
  return self;
}
SYMBOL_PUBLIC sx_t *sx_reserve(sx_t *self, size_t min_cap) {
  return sx__reserve(self->ref, min_cap) ? NULL : self;
}

SYMBOL_PUBLIC sx_t *sx_substr(const sx_t *h, size_t idx, size_t n) {
  sx_t *s = sx_new_with_cap(n + 1); /* content + terminator */
  if (!s)
    return NULL;
  memcpy(s->ref->ptr, h->ref->ptr + idx, n * sizeof(char));
  s->ref->len = n;
  s->ref->ptr[n] = '\0';
  return s;
}

SYMBOL_PUBLIC sx_view_t sx_view_advance(sx_view_t v, size_t n) {
  if (v.offset + v.len + n < v.ref->len) {
    v.offset += n;
    v.len -= n;
    return v;
  }
  if (v.offset + n < v.ref->len) {
    v.offset += n;
    v.len = v.ref->len - v.offset;
    return v;
  }
  v.offset = v.ref->len;
  v.len = 0;
  return v;
}
SYMBOL_PUBLIC sx_view_t sx_view_retreat(sx_view_t v, size_t n) {
  if (v.offset < n) {
    v.len += v.offset;
    v.offset = 0;
    return v;
  }
  v.offset -= n;
  v.len += n;
  return v;
}
SYMBOL_PUBLIC sx_view_t sx_view_shift(sx_view_t v, ptrdiff_t off) {
  if (off < 0) {
    size_t delta = (size_t)(-off);
    size_t new_off = v.offset - delta;
    if (new_off > v.offset)
      new_off = 0;
    v.offset = new_off;
  } else {
    size_t delta = (size_t)off;
    if (delta > v.ref->len - v.len - v.offset)
      v.offset = v.ref->len - v.len;
    else
      v.offset += delta;
  }
  return v;
}
SYMBOL_PUBLIC sx_view_t sx_view_range(sx_view_t v, size_t off, size_t len) {
  if (len + off > v.ref->len)
    len = v.ref->len - off;
  v.offset = off;
  v.len = len;
  return v;
}
static inline int is_trim(char c) {
  if (c == ' ')
    return 1;
  if (c == '\r')
    return 1;
  if (c == '\n')
    return 1;
  if (c == '\t')
    return 1;
  if (c == '\v')
    return 1;
  if (c == '\f')
    return 1;
  return 0;
}
SYMBOL_PUBLIC sx_view_t sx_view_trim(sx_view_t v) {
  v = sx_view_trim_front(v);
  v = sx_view_trim_back(v);
  return v;
}
SYMBOL_PUBLIC sx_view_t sx_view_trim_front(sx_view_t v) {
  while (v.offset < v.ref->len && v.len > 0 && is_trim(v.ref->ptr[v.offset]))
    v.offset++,v.len--;
  return v;
}
SYMBOL_PUBLIC sx_view_t sx_view_trim_back(sx_view_t v) {
  {
    while (v.len > 0 && is_trim(v.ref->ptr[v.offset + v.len - 1]))
      v.len--;
    return v;
  }
}

#if SIZE_MAX > UINTPTR_MAX
typedef size_t find_ret_t;
#else
typedef uintptr_t find_ret_t;
#endif

/* memrchr on glibc is the memchr-speed reverse primitive; elsewhere
   (and in the test build with -DSX_FORCE_NO_MEMRCHR) fall back to a
   byte-wise backward scan. */
#if !defined(SX_FORCE_NO_MEMRCHR) && defined(__GLIBC__)
extern void *memrchr(const void *s, int c, size_t n);
#define SX_USE_MEMRCHR 1
#else
#define SX_USE_MEMRCHR 0
#endif

/*
 * Internal search primitive.  Byte-exact (memcmp semantics) search for
 * needle[0 .. len) inside h's content region; an embedded '\0' in the
 * needle is an ordinary byte (binary-safe).
 *
 * opt bit0 (1): "right" -- `boundary` bounds the search region FROM THE
 *     RIGHT: region = [0, min(boundary, H)) where H = content length.
 * opt bit0 clear: "left" -- region = [boundary, H).
 * A match window [p, p+m) must fall entirely inside the region
 * (p >= region start, p + m <= region end).
 * Whole-string search: leftmost  -> boundary = 0;
 *                      rightmost -> boundary = h->ref->len, right.
 *
 * opt bit1 (2): "first" -- single result: leftmost (or, with right,
 *     rightmost) match position, or (find_ret_t)-1 if none.
 *     No allocation.
 * opt bit1 clear: ALL mode -- malloc'd size_t array, caller frees:
 *     arr[0] = count, arr[1..count] = positions in ascending order
 *     (overlapping matches included).  count == 0 -> a valid {0} array.
 *     Returns 0 on allocation failure.
 *
 * len <= 0, or a region with no room for a window: not found --
 * FIRST mode -> (find_ret_t)-1, ALL mode -> {0} array.
 */
static inline find_ret_t find(const sx_t *h, const char *needle, int len,
                              size_t boundary, int opt) {
  int right = opt & 1;          /* boundary bounds the region from the right */
  int first = opt & 2;          /* single-match scalar return */
  const size_t H = h->ref->len; /* content bytes of h */
  const size_t m = (size_t)len;
  const char *t = h->ref->ptr;

  /* region [l, er): end clamp handles boundary >= H for right search */
  size_t l = right ? 0 : boundary;
  size_t er = right ? (boundary < H ? boundary : H) : H;

  /* Empty/negative needle, or region too small for a window: not found,
     reported per-mode. */
  if (len <= 0 || l >= er || m > er - l) {
    if (first)
      return (find_ret_t)-1;
    size_t *r = malloc(sizeof(size_t));
    if (!r)
      return 0;
    r[0] = 0;
    return (find_ret_t)r;
  }

  if (m == 1) {
    if (first) {
      const char *p;
      if (right) {
#if SX_USE_MEMRCHR
        p = memrchr(t, *needle, er);
#else
        /* backward scan: visits t[er-1] .. t[l], keeps the LAST (i.e.
           rightmost) hit; loop exits before i can underflow (l == 0) */
        p = NULL;
        for (size_t i = er; i > l; i--)
          if (t[i - 1] == *needle) {
            p = t + (i - 1);
            break;
          }
#endif
      } else {
        p = memchr(t + l, *needle, er - l);
      }
      return p ? (find_ret_t)(p - t) : (find_ret_t)-1;
    }
    /* ALL mode: ascending positions over the region [l, er) */
    size_t cap = 16;
    size_t *r = malloc(cap * sizeof(*r));
    if (!r)
      return 0;
    size_t cnt = 0;
    const char *at = memchr(t + l, *needle, er - l);
    while (at) {
      if (cnt + 1 >= cap) { /* next write is r[1+cnt]; need 1+cnt <= cap-1 */
        if (cap > SIZE_MAX / 2 / sizeof(*r)) {
          free(r);
          return 0;
        }
        cap *= 2;
        size_t *nr = realloc(r, cap * sizeof(*r));
        if (!nr) {
          free(r);
          return 0;
        }
        r = nr;
      }
      r[1 + cnt++] = (size_t)(at - t);
      at = memchr(at + 1, *needle, er - (size_t)(at - t) - 1);
    }
    void *nr = realloc(r, (cnt + 1) * sizeof(*r));
    if (nr)
      r = nr;
    r[0] = cnt;
    return (find_ret_t)r;
  }

  /* m >= 2 */
  size_t *pi = malloc(m * sizeof(*pi));
  if (!pi)
    return first ? (find_ret_t)-1 : 0;

  if (first && right) {
    /* reverse KMP: pi over the REVERSED needle (index-mapped, no copy);
       scan the text right-to-left within the region.  Invariant: after
       processing text index i, the last j bytes of needle
       (needle[m-1-j .. m-1]) match text[i .. i+j-1]; when j reaches m
       the window start is i itself. */
    pi[0] = 0;
    for (size_t k = 0, i = 1; i < m; i++) {
      char a = needle[m - 1 - i];
      while (k && a != needle[m - 1 - k])
        k = pi[k - 1];
      if (a == needle[m - 1 - k])
        k++;
      pi[i] = k;
    }
    size_t j = 0;
    for (size_t i = er; i-- > l;) {
      while (j && t[i] != needle[m - 1 - j])
        j = pi[j - 1];
      if (t[i] == needle[m - 1 - j])
        j++;
      if (j == m) {
        free(pi);
        return (find_ret_t)i; /* discovery index is the window start */
      }
    }
    free(pi);
    return (find_ret_t)-1;
  }

  /* forward KMP over the region [l, er) (used by leftmost-FIRST and by
     ALL mode) */
  pi[0] = 0;
  for (size_t k = 0, i = 1; i < m; i++) {
    while (k && needle[i] != needle[k])
      k = pi[k - 1];
    if (needle[i] == needle[k])
      k++;
    pi[i] = k;
  }

  size_t *r = NULL;
  size_t cap = 0, cnt = 0;
  if (!first) {
    cap = 16;
    r = malloc(cap * sizeof(*r));
    if (!r) {
      free(pi);
      return 0;
    }
  }
  size_t q = 0; /* bytes of needle matched so far */
  for (size_t i = l; i < er; i++) {
    while (q && t[i] != needle[q])
      q = pi[q - 1];
    if (t[i] == needle[q])
      q++;
    if (q == m) {
      size_t pos = i + 1 - m;
      if (first) {
        free(pi);
        return (find_ret_t)pos;
      }
      if (cnt + 1 >= cap) { /* next write is r[1+cnt]; need 1+cnt <= cap-1 */
        if (cap > SIZE_MAX / 2 / sizeof(*r)) {
          free(pi);
          free(r);
          return 0;
        }
        cap *= 2;
        size_t *nr = realloc(r, cap * sizeof(*r));
        if (!nr) {
          free(pi);
          free(r);
          return 0;
        }
        r = nr;
      }
      r[1 + cnt++] = pos;
      q = pi[m - 1];
    }
  }
  free(pi);
  if (first)
    return (find_ret_t)-1;
  void *nr = realloc(r, (cnt + 1) * sizeof(*r));
  if (nr)
    r = nr; /* on shrink failure keep the oversized block: still valid */
  r[0] = cnt;
  return (find_ret_t)r;
}
/* Wrappers translate the public API onto find().  Per the buffer model,
   a needle's searchable length is its CONTENT length (the trailing '\0'
   at ptr[len] is not content): sx_find* pass the needle's own length,
   sx_rfind* also search the full string (boundary = h's content length),
   and the find_all* family returns NULL when nothing matched. */
SYMBOL_PUBLIC size_t sx_find(const sx_t *h, const sx_t *needle) {
  if (!h || !needle)
    return (size_t)-1;
  return (size_t)find(h, needle->ref->ptr, needle->ref->len, 0, 2);
}
SYMBOL_PUBLIC size_t sx_find_view(const sx_t *h, sx_view_t needle) {
  return sx_find_view_b(h, needle, 0);
}
// Bounded variant of sx_find_view: search only inside [boundary, len(h)).
SYMBOL_PUBLIC size_t sx_find_view_b(const sx_t *h, sx_view_t needle,
                                    size_t boundary) {
  if (!h)
    return (size_t)-1;
  return (size_t)find(h, needle.ref->ptr + needle.offset, needle.len, boundary,
                      2);
}
SYMBOL_PUBLIC size_t sx_find_cstr(const sx_t *h, const char *needle) {
  if (!h || !needle)
    return (size_t)-1;
  return (size_t)find(h, needle, (int)strlen(needle), 0, 2);
}
SYMBOL_PUBLIC size_t sx_find_char(const sx_t *h, char c) {
  if (!h)
    return (size_t)-1;
  return (size_t)find(h, &c, 1, 0, 2);
}
SYMBOL_PUBLIC size_t sx_rfind(const sx_t *h, const sx_t *needle) {
  if (!h || !needle)
    return (size_t)-1;
  return (size_t)find(h, needle->ref->ptr, needle->ref->len, h->ref->len, 3);
}
SYMBOL_PUBLIC size_t sx_rfind_view(const sx_t *h, sx_view_t needle) {
  if (!h)
    return (size_t)-1;
  return sx_rfind_view_b(h, needle, h->ref->len);
}
// Bounded variant of sx_rfind_view: search only inside
// [0, min(boundary, len(h))).
SYMBOL_PUBLIC size_t sx_rfind_view_b(const sx_t *h, sx_view_t needle,
                                     size_t boundary) {
  if (!h)
    return (size_t)-1;
  return (size_t)find(h, needle.ref->ptr + needle.offset, needle.len, boundary,
                      3);
}
SYMBOL_PUBLIC size_t sx_rfind_cstr(const sx_t *h, const char *needle) {
  if (!h || !needle)
    return (size_t)-1;
  return (size_t)find(h, needle, (int)strlen(needle), h->ref->len, 3);
}
SYMBOL_PUBLIC size_t sx_rfind_char(const sx_t *h, char c) {
  if (!h)
    return (size_t)-1;
  return (size_t)find(h, &c, 1, h->ref->len, 3);
}
// Returns: {count, pos0, pos1, ...}; caller must free the memory.
// Returns NULL if not found.
static size_t *find_all_ret(find_ret_t r) {
  size_t *ret = (size_t *)r;
  if (!ret)
    return NULL;     /* allocation failure */
  if (ret[0] == 0) { /* not found: public contract says NULL */
    free(ret);
    return NULL;
  }
  return ret;
}
SYMBOL_PUBLIC size_t *sx_find_all(const sx_t *h, const sx_t *needle) {
  if (!h || !needle)
    return NULL;
  return find_all_ret(find(h, needle->ref->ptr, needle->ref->len, 0, 0));
}
SYMBOL_PUBLIC size_t *sx_find_all_view(const sx_t *h, sx_view_t needle) {
  if (!h)
    return NULL;
  return find_all_ret(
      find(h, needle.ref->ptr + needle.offset, needle.len, 0, 0));
}
SYMBOL_PUBLIC size_t *sx_find_all_cstr(const sx_t *h, const char *needle) {
  if (!h || !needle)
    return NULL;
  return find_all_ret(find(h, needle, (int)strlen(needle), 0, 0));
}
SYMBOL_PUBLIC size_t *sx_find_all_char(const sx_t *h, char c) {
  if (!h)
    return NULL;
  return find_all_ret(find(h, &c, 1, 0, 0));
}

static inline int cmp(const sx_t *h, const char *other, size_t len) {
  size_t n = h->ref->len < len ? h->ref->len : len;
  int r = memcmp(h->ref->ptr, other, n);
  if (r != 0)
    return r;
  if (h->ref->len < len)
    return -1;
  if (h->ref->len > len)
    return 1;
  return 0;
}

SYMBOL_PUBLIC int sx_cmp(const sx_t *h, const sx_t *other) {
  if (!h || !other)
    return 0;
  return cmp(h, other->ref->ptr, other->ref->len);
}
SYMBOL_PUBLIC int sx_cmp_view(const sx_t *h, sx_view_t other) {
  if (!h)
    return 0;
  return cmp(h, other.ref->ptr + other.offset, other.len);
}
SYMBOL_PUBLIC int sx_cmp_cstr(const sx_t *h, const char *s) {
  if (!h || !s)
    return 0;
  return cmp(h, s, strlen(s));
}

SYMBOL_PUBLIC int sx_starts_with(const sx_t *h, const sx_t *prefix) {
  if (!h || !prefix)
    return 0;
  size_t n = prefix->ref->len;
  if (n > h->ref->len)
    return 0;
  return memcmp(h->ref->ptr, prefix->ref->ptr, n) == 0;
}
SYMBOL_PUBLIC int sx_starts_with_view(const sx_t *h, sx_view_t prefix) {
  if (!h)
    return 0;
  size_t n = prefix.len;
  if (n > h->ref->len)
    return 0;
  return memcmp(h->ref->ptr, prefix.ref->ptr + prefix.offset, n) == 0;
}
SYMBOL_PUBLIC int sx_starts_with_cstr(const sx_t *h, const char *prefix) {
  if (!h || !prefix)
    return 0;
  size_t n = strlen(prefix);
  if (n > h->ref->len)
    return 0;
  return memcmp(h->ref->ptr, prefix, n) == 0;
}
SYMBOL_PUBLIC int sx_ends_with(const sx_t *h, const sx_t *suffix) {
  if (!h || !suffix)
    return 0;
  size_t n = suffix->ref->len;
  if (n > h->ref->len)
    return 0;
  return memcmp(h->ref->ptr + h->ref->len - n, suffix->ref->ptr, n) == 0;
}
SYMBOL_PUBLIC int sx_ends_with_view(const sx_t *h, sx_view_t suffix) {
  if (!h)
    return 0;
  size_t n = suffix.len;
  if (n > h->ref->len)
    return 0;
  return memcmp(h->ref->ptr + h->ref->len - n, suffix.ref->ptr + suffix.offset,
                n) == 0;
}
SYMBOL_PUBLIC int sx_ends_with_cstr(const sx_t *h, const char *suffix) {
  if (!h || !suffix)
    return 0;
  size_t n = strlen(suffix);
  if (n > h->ref->len)
    return 0;
  return memcmp(h->ref->ptr + h->ref->len - n, suffix, n) == 0;
}
