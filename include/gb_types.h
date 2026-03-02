#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;

static inline u16 u16_lohi(u8 lo, u8 hi) { return (u16)lo | ((u16)hi << 8); }
static inline u8  lo8(u16 v) { return (u8)(v & 0xFF); }
static inline u8  hi8(u16 v) { return (u8)(v >> 8); }
