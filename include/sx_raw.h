#ifndef SX_RAW
#define SX_RAW

#include <stdlib.h>

typedef struct _sx _sx;

_sx *sx__new(size_t cap);
_sx *sx__dup(const _sx *src);
int sx__reserve(_sx *s, size_t need);

void sx__free(_sx *s);

#endif