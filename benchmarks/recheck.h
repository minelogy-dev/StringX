// SPDX-License-Identifier: MIT
#ifndef RECHECK_H
#define RECHECK_H
#include <stddef.h>
#include <stdint.h>
#include <sx_core.h>
#include <sds.h>
#include <bstrlib.h>

typedef struct {
  sx_t *sx, *sx_pattern;
  sds sd;
  bstring bs, bs_pattern;
  unsigned char *pattern;
  size_t n, m;
  const unsigned char *text_bytes[4], *pattern_bytes[4];
} recheck_input;

typedef uint64_t (*recheck_fn)(recheck_input *);
uint64_t recheck_sx(recheck_input *);
uint64_t recheck_sds(recheck_input *);
uint64_t recheck_bstr(recheck_input *);
uint64_t recheck_raw(recheck_input *);
extern uint64_t recheck_call_counts[4];
#endif
