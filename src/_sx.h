#ifndef CSTR
#define CSTR

#include <stdlib.h>

/*
 * _sx buffer model (single NUL terminator):
 *   ptr[0 .. len-1] -> content bytes;           len == content length
 *   ptr[len]        -> always '\0'
 *   cap             -> total bytes allocated for ptr;  len <= cap - 1
 */
typedef struct _sx {
    char *ptr;
    size_t len, cap;
} _sx;

#endif