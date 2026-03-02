#pragma once
#include "gb_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct APU {
    int dummy;
} APU;

void apu_reset(APU* a);
void apu_tick(struct GB* gb, int cycles);

#ifdef __cplusplus
}
#endif
