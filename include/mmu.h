#pragma once
#include "gb_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GB GB;

u8  mmu_read8(GB* gb, u16 addr);
u16 mmu_read16(GB* gb, u16 addr);
void mmu_write8(GB* gb, u16 addr, u8 v);
void mmu_write16(GB* gb, u16 addr, u16 v);

#ifdef __cplusplus
}
#endif
