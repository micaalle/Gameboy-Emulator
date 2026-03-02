#pragma once
#include "gb_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PPU {
    int dot;      
    u8  ly;       
    u8  mode;     

    u8  prev_mode;
    bool prev_lyc;

    u32 framebuffer[160 * 144];
} PPU;

void ppu_reset(PPU* p);
void ppu_tick(struct GB* gb, int cycles);

#ifdef __cplusplus
}
#endif
