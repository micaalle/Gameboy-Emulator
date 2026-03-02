#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

void die(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

void* xmalloc(size_t n) {
    void* p = malloc(n);
    if (!p) die("out of memory (%zu bytes)", n);
    return p;
}

void* xcalloc(size_t n, size_t sz) {
    void* p = calloc(n, sz);
    if (!p) die("out of memory (%zu bytes)", n * sz);
    return p;
}
