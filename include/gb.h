#pragma once
#include "gb_types.h"
#include "cpu.h"
#include "ppu.h"
#include "timer.h"
#include "apu.h"
#include "cart.h"

#ifdef __cplusplus
extern "C" {
#endif

// interrupt flags
enum {
    INT_VBLANK = 1 << 0,
    INT_STAT   = 1 << 1,
    INT_TIMER  = 1 << 2,
    INT_SERIAL = 1 << 3,
    INT_JOYPAD = 1 << 4,
};

typedef struct GB {
    CPU cpu;
    PPU ppu;
    Timer timer;
    APU apu;
    Cart cart;

    u8 vram[0x2000]; // 8KB
    u8 wram[0x2000]; // 8KB
    u8 oam[0xA0];    // 160 bytes
    u8 hram[0x7F];   // 127 bytes

    // IO registers
    u8 io[0x80];     // FF00-FF7F

    u8 ie;

    u8 joyp_select; 
    u8 joyp_buttons; 
    u8 joyp_dpad;    

    bool dma_active;
    u16 dma_src;
    int dma_ticks; 
} GB;

bool gb_init(GB* gb, const char* rom_path);
void gb_free(GB* gb);
void gb_reset(GB* gb);


void gb_tick(GB* gb, int cycles);

void gb_run_frame(GB* gb);


void gb_request_interrupt(GB* gb, u8 which);


static inline u8 gb_if(GB* gb) { return gb->io[0x0F]; }
static inline void gb_set_if(GB* gb, u8 v) { gb->io[0x0F] = v; }

#ifdef __cplusplus
}
#endif
