#pragma once
#include "gb_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void die(const char* fmt, ...);
void* xmalloc(size_t n);
void* xcalloc(size_t n, size_t sz);

#ifdef __cplusplus
}
#endif
