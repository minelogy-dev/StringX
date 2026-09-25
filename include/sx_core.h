#ifndef SX_CORE
#define SX_CORE

#include <stddef.h>

/*
 * Buffer model (single NUL terminator):
 *   ptr[0 .. len-1] -> content bytes;     len == content length.
 *   ptr[len]        -> always '\0'.
 *   cap             -> total bytes allocated for ptr; len <= cap - 1.
 *   len == 0 means an empty (but valid, NUL-terminated) buffer.
 *
 * sx_len(): number of content bytes (== len).  Does NOT stop at
 *           embedded '\0'.
 */
typedef struct _sx _sx;
typedef struct sx_t {
  _sx *ref;
} sx_t;
typedef struct sx_view_t {
  _sx *ref;
  size_t offset, len;
} sx_view_t;

// The return values of these functions must be freed with sx_free()
sx_t *sx_new(void);
sx_t *sx_new_from_cstr(const char *s);
sx_t *sx_new_from_view(sx_view_t v);
sx_t *sx_new_with_cap(size_t n);
sx_t *sx_dup(const sx_t *src);

void sx_free(sx_t *h);

int sx_len(sx_t *h);
int sx_len_view(sx_view_t v);
int sx_cap(sx_t *h);
char *sx_buf_mut(sx_t *h);
char *sx_buf_view_mut(sx_view_t v);

sx_view_t sx_view(const sx_t *h);

sx_t *sx_append(sx_t *self, sx_t *owned);
sx_t *sx_append_cstr(sx_t *self, const char *s);
sx_t *sx_append_view(sx_t *self, sx_view_t v);
sx_t *sx_write(sx_t *self, size_t idx, sx_t *owned);
sx_t *sx_write_cstr(sx_t *self, size_t idx, const char *s);
sx_t *sx_write_view(sx_t *self, size_t idx, sx_view_t v);
sx_t *sx_insert(sx_t *self, size_t idx, sx_t *owned);
sx_t *sx_insert_cstr(sx_t *self, size_t idx, const char *s);
sx_t *sx_insert_view(sx_t *self, size_t idx, sx_view_t v);
sx_t *sx_truncate(sx_t *self, size_t new_len);
sx_t *sx_erase(sx_t *self, size_t idx, size_t n);
sx_t *sx_clear(sx_t *self);
sx_t *sx_reserve(sx_t *self, size_t min_cap);
// Warning: Unlike the other functions, the returned pointer
// is owned by the caller and must be freed with sx_free().
sx_t *sx_substr(const sx_t *h, size_t idx, size_t n);

sx_view_t sx_view_advance(sx_view_t v, size_t n);
sx_view_t sx_view_retreat(sx_view_t v, size_t n);
sx_view_t sx_view_shift(sx_view_t v, ptrdiff_t off);
sx_view_t sx_view_range(sx_view_t v, size_t off, size_t len);
sx_view_t sx_view_trim(sx_view_t v);
sx_view_t sx_view_trim_front(sx_view_t v);
sx_view_t sx_view_trim_back(sx_view_t v);

// Finds the leftmost occurrence of `needle` in h (left search over the
// whole string).  Returns the byte offset of the match, or (size_t)-1 if
// it is not found.
size_t sx_find(const sx_t *h, const sx_t *needle);
size_t sx_find_view(const sx_t *h, sx_view_t needle);
// Bounded variant: search only inside the region [boundary, len(h)).
size_t sx_find_view_b(const sx_t *h, sx_view_t needle, size_t boundary);
size_t sx_find_cstr(const sx_t *h, const char *needle);
size_t sx_find_char(const sx_t *h, char c);
// Finds the rightmost occurrence of `needle` in h (right search over the
// whole string).  Returns the byte offset of the match, or (size_t)-1 if
// it is not found.
size_t sx_rfind(const sx_t *h, const sx_t *needle);
size_t sx_rfind_view(const sx_t *h, sx_view_t needle);
// Bounded variant: search only inside the region
// [0, min(boundary, len(h))).
size_t sx_rfind_view_b(const sx_t *h, sx_view_t needle, size_t boundary);
size_t sx_rfind_cstr(const sx_t *h, const char *needle);
size_t sx_rfind_char(const sx_t *h, char c);
// Returns: {count, pos0, pos1, ...}; caller must free the memory.
// Returns NULL if not found.
size_t *sx_find_all(const sx_t *h, const sx_t *needle);
size_t *sx_find_all_view(const sx_t *h, sx_view_t needle);
size_t *sx_find_all_cstr(const sx_t *h, const char *needle);
size_t *sx_find_all_char(const sx_t *h, char c);

int sx_cmp(const sx_t *h, const sx_t *other);
int sx_cmp_view(const sx_t *h, sx_view_t other);
int sx_cmp_cstr(const sx_t *h, const char *s);

int sx_starts_with(const sx_t *h, const sx_t *prefix);
int sx_starts_with_view(const sx_t *h, sx_view_t prefix);
int sx_starts_with_cstr(const sx_t *h, const char *prefix);
int sx_ends_with(const sx_t *h, const sx_t *suffix);
int sx_ends_with_view(const sx_t *h, sx_view_t suffix);
int sx_ends_with_cstr(const sx_t *h, const char *suffix);
#endif