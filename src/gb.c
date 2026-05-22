#include "gb.h"
#include "mmu.h"
#include "util.h"
#include <string.h>

static void io_reset_defaults(GB* gb) {
    memset(gb->io, 0, sizeof(gb->io));

    gb->io[0x00] = 0xCF; // JOYP
    gb->io[0x04] = 0xAB; // DIV 
    gb->io[0x05] = 0x00; // TIMA
    gb->io[0x06] = 0x00; // TMA
    gb->io[0x07] = 0xF8; // TAC upper bits 1

    gb->io[0x0F] = 0xE1; 

    gb->io[0x10] = 0x80; // NR10
    gb->io[0x11] = 0xBF; // NR11
    gb->io[0x12] = 0xF3; // NR12
    gb->io[0x14] = 0xBF; // NR14
    gb->io[0x16] = 0x3F; // NR21
    gb->io[0x19] = 0xBF; // NR24
    gb->io[0x1A] = 0x7F; // NR30
    gb->io[0x1B] = 0xFF; // NR31
    gb->io[0x1C] = 0x9F; // NR32
    gb->io[0x1E] = 0xBF; // NR34
    gb->io[0x20] = 0xFF; // NR41
    gb->io[0x23] = 0xBF; // NR44
    gb->io[0x24] = 0x77; // NR50 master volume
    gb->io[0x25] = 0xF3; // NR51 
    gb->io[0x26] = 0xF1; // NR52 master enable

    gb->io[0x40] = 0x91; // LCDC
    gb->io[0x41] = 0x85; // STAT
    gb->io[0x42] = 0x00; // SCY
    gb->io[0x43] = 0x00; // SCX
    gb->io[0x44] = 0x00; // LY
    gb->io[0x45] = 0x00; // LYC
    gb->io[0x47] = 0xFC; // BGP
    gb->io[0x48] = 0xFF; // OBP0
    gb->io[0x49] = 0xFF; // OBP1
    gb->io[0x4A] = 0x00; // WY
    gb->io[0x4B] = 0x00; // WX
}

bool gb_init(GB* gb, const char* rom_path) {
    memset(gb, 0, sizeof(*gb));
    if (!cart_load(&gb->cart, rom_path)) return false;

    gb_reset(gb);
    return true;
}

void gb_free(GB* gb) {
    cart_free(&gb->cart);
}

void gb_reset(GB* gb) {
    // clear RAM
    memset(gb->vram, 0, sizeof(gb->vram));
    memset(gb->wram, 0, sizeof(gb->wram));
    memset(gb->oam,  0, sizeof(gb->oam));
    memset(gb->hram, 0, sizeof(gb->hram));

    // reset
    cpu_reset(&gb->cpu);
    ppu_reset(&gb->ppu);
    timer_reset(&gb->timer);
    apu_reset(&gb->apu);

    io_reset_defaults(gb);

    gb->ie = 0x00;

    // 1 now pressed
    gb->joyp_select  = 0x30;
    gb->joyp_buttons = 0x0F;
    gb->joyp_dpad    = 0x0F;

    // DMA
    gb->dma_active = false;
    gb->dma_src = 0;
    gb->dma_ticks = 0;
}

void gb_request_interrupt(GB* gb, u8 which) {
    u8 v = gb_if(gb);
    v |= which;
    v |= 0xE0;
    gb_set_if(gb, v);
}

static void dma_tick(GB* gb, int cycles) {
    if (!gb->dma_active) return;

    while (cycles >= 4 && gb->dma_ticks > 0) {
        int index = 160 - gb->dma_ticks;
        u8 b = mmu_read8(gb, gb->dma_src + (u16)index);
        gb->oam[index] = b;
        gb->dma_ticks--;
        cycles -= 4;
    }

    if (gb->dma_ticks <= 0) {
        gb->dma_active = false;
        gb->dma_src = 0;
    }
}

void gb_tick(GB* gb, int cycles) {
    dma_tick(gb, cycles);

    timer_tick(gb, cycles);
    ppu_tick(gb, cycles);
    apu_tick(gb, cycles);
}

void gb_run_frame(GB* gb) {
    const int frame_cycles = 70224;
    int spent = 0;

    while (spent < frame_cycles) {
        int c = cpu_step(gb);
        gb_tick(gb, c);
        spent += c;
    }
}
