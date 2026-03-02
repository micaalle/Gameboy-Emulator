#include "mmu.h"
#include "gb.h"
#include "cart.h"
#include <string.h>

static inline u8 io_read(GB* gb, u16 addr) {
    u8 idx = (u8)(addr & 0x7F);

    if (idx == 0x00) {

        u8 sel = gb->joyp_select & 0x30;
        u8 low = 0x0F;
        if (!(sel & 0x10)) { 
            low &= gb->joyp_dpad;
        }
        if (!(sel & 0x20)) { 
            low &= gb->joyp_buttons;
        }
        return (u8)(0xC0 | sel | low); 
    }

    if (idx == 0x0F) {
        return (u8)(gb->io[idx] | 0xE0);
    }

    return gb->io[idx];
}

static inline void io_write(GB* gb, u16 addr, u8 v) {
    u8 idx = (u8)(addr & 0x7F);

    switch (idx) {
        case 0x00: // JOYP
            gb->joyp_select = (u8)(v & 0x30);
            gb->io[idx] = (u8)(0xC0 | gb->joyp_select | 0x0F);
            break;
        case 0x04: // DIV
            gb->io[idx] = 0;
            gb->timer.div_counter = 0;
            break;
        case 0x05: // TIMA
        case 0x06: // TMA
        case 0x07: // TAC
            gb->io[idx] = v;
            break;
        case 0x0F: // IF
            gb->io[idx] = (u8)(v | 0xE0);
            break;
        case 0x44: // LY
            gb->io[idx] = 0;
            gb->ppu.ly = 0;
            break;
        case 0x46: { // DMA
            gb->io[idx] = v;
            gb->dma_active = true;
            gb->dma_src = (u16)v << 8;
            gb->dma_ticks = 160;
        } break;
        default:
            gb->io[idx] = v;
            break;
    }
}

u8 mmu_read8(GB* gb, u16 addr) {
    if (addr <= 0x7FFF) {
        return cart_read_rom(&gb->cart, addr);
    }
    if (addr >= 0x8000 && addr <= 0x9FFF) {
        return gb->vram[addr - 0x8000];
    }
    if (addr >= 0xA000 && addr <= 0xBFFF) {
        return cart_read_ram(&gb->cart, addr);
    }
    if (addr >= 0xC000 && addr <= 0xDFFF) {
        return gb->wram[addr - 0xC000];
    }
    if (addr >= 0xE000 && addr <= 0xFDFF) {
        // echo
        return gb->wram[addr - 0xE000];
    }
    if (addr >= 0xFE00 && addr <= 0xFE9F) {
        return gb->oam[addr - 0xFE00];
    }
    if (addr >= 0xFEA0 && addr <= 0xFEFF) {
        // unusable
        return 0xFF;
    }
    if (addr >= 0xFF00 && addr <= 0xFF7F) {
        return io_read(gb, addr);
    }
    if (addr >= 0xFF80 && addr <= 0xFFFE) {
        return gb->hram[addr - 0xFF80];
    }
    if (addr == 0xFFFF) {
        return gb->ie;
    }
    return 0xFF;
}

u16 mmu_read16(GB* gb, u16 addr) {
    u8 lo = mmu_read8(gb, addr);
    u8 hi = mmu_read8(gb, (u16)(addr + 1));
    return u16_lohi(lo, hi);
}

void mmu_write8(GB* gb, u16 addr, u8 v) {
    if (addr <= 0x7FFF) {
        cart_write_rom(&gb->cart, addr, v);
        return;
    }
    if (addr >= 0x8000 && addr <= 0x9FFF) {
        gb->vram[addr - 0x8000] = v;
        return;
    }
    if (addr >= 0xA000 && addr <= 0xBFFF) {
        cart_write_ram(&gb->cart, addr, v);
        return;
    }
    if (addr >= 0xC000 && addr <= 0xDFFF) {
        gb->wram[addr - 0xC000] = v;
        return;
    }
    if (addr >= 0xE000 && addr <= 0xFDFF) {
        gb->wram[addr - 0xE000] = v;
        return;
    }
    if (addr >= 0xFE00 && addr <= 0xFE9F) {
        gb->oam[addr - 0xFE00] = v;
        return;
    }
    if (addr >= 0xFEA0 && addr <= 0xFEFF) {
        return;
    }
    if (addr >= 0xFF00 && addr <= 0xFF7F) {
        io_write(gb, addr, v);
        return;
    }
    if (addr >= 0xFF80 && addr <= 0xFFFE) {
        gb->hram[addr - 0xFF80] = v;
        return;
    }
    if (addr == 0xFFFF) {
        gb->ie = v;
        return;
    }
}

void mmu_write16(GB* gb, u16 addr, u16 v) {
    mmu_write8(gb, addr, lo8(v));
    mmu_write8(gb, (u16)(addr + 1), hi8(v));
}
