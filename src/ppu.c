#include "ppu.h"
#include "gb.h"
#include "mmu.h"
#include <string.h>

static inline u32 shade_from_palette(u8 pal, u8 color_idx) {
    u8 shade = (pal >> (color_idx * 2)) & 0x03;
    switch (shade) {
        default:
        case 0: return 0xFFFFFFFFu; // white
        case 1: return 0xFFAAAAAAu;
        case 2: return 0xFF555555u;
        case 3: return 0xFF000000u; // black
    }
}

static inline u8 vram_read(GB* gb, u16 addr) {
    // addr in 0x8000,0x9FFF
    return gb->vram[addr - 0x8000];
}

static inline u8 tile_pixel(GB* gb, u16 tile_base, int tile_id, int x_in, int y_in, bool signed_index) {
    u16 tile_addr;
    if (!signed_index) {
        tile_addr = (u16)(tile_base + (u16)(tile_id * 16));
    } else {
        s8 sid = (s8)(u8)tile_id;

        tile_addr = (u16)(0x9000 + (s16)sid * 16);
    }
    int row = y_in & 7;
    u8 lo = vram_read(gb, (u16)(tile_addr + row * 2));
    u8 hi = vram_read(gb, (u16)(tile_addr + row * 2 + 1));
    int bit = 7 - (x_in & 7);
    u8 b0 = (lo >> bit) & 1;
    u8 b1 = (hi >> bit) & 1;
    return (u8)((b1 << 1) | b0);
}

static void render_scanline(GB* gb) {
    const u8 lcdc = gb->io[0x40];
    const u8 scy  = gb->io[0x42];
    const u8 scx  = gb->io[0x43];
    const u8 wy   = gb->io[0x4A];
    const u8 wx   = gb->io[0x4B];
    const u8 bgp  = gb->io[0x47];
    const u8 obp0 = gb->io[0x48];
    const u8 obp1 = gb->io[0x49];

    const bool bg_enable = (lcdc & 0x01) != 0;
    const bool obj_enable = (lcdc & 0x02) != 0;
    const bool obj_16 = (lcdc & 0x04) != 0;
    const bool bg_map_hi = (lcdc & 0x08) != 0;
    const bool tile_data_hi = (lcdc & 0x10) != 0;
    const bool win_enable = (lcdc & 0x20) != 0;
    const bool win_map_hi = (lcdc & 0x40) != 0;

    const u16 bg_map = (u16)(bg_map_hi ? 0x9C00 : 0x9800);
    const u16 win_map = (u16)(win_map_hi ? 0x9C00 : 0x9800);
    const u16 tile_base = (u16)(tile_data_hi ? 0x8000 : 0x8800);
    const bool signed_index = !tile_data_hi;

    const int y = (int)gb->ppu.ly;
    if (y >= 144) return;

    for (int x = 0; x < 160; x++) {
        u8 bg_col = 0;

        // background 
        if (bg_enable) {
            bool use_win = false;
            int win_x0 = (int)wx - 7;
            if (win_enable && y >= (int)wy && x >= win_x0) use_win = true;

            int px, py;
            u16 map_base;
            if (use_win) {
                px = x - win_x0;
                py = y - (int)wy;
                map_base = win_map;
            } else {
                px = (x + scx) & 0xFF;
                py = (y + scy) & 0xFF;
                map_base = bg_map;
            }

            int tx = (px >> 3) & 31;
            int ty = (py >> 3) & 31;
            int map_index = ty * 32 + tx;
            u8 tile_id = vram_read(gb, (u16)(map_base + map_index));

            bg_col = tile_pixel(gb, tile_base, tile_id, px & 7, py & 7, signed_index);
        }

        u32 out = shade_from_palette(bgp, bg_col);

        // sprites
        if (obj_enable) {
            int sprite_h = obj_16 ? 16 : 8;
            int sprites_on_line = 0;

            for (int i = 0; i < 40 && sprites_on_line < 10; i++) {
                int o = i * 4;
                int sy = (int)gb->oam[o + 0] - 16;
                int sx = (int)gb->oam[o + 1] - 8;
                u8  tid = gb->oam[o + 2];
                u8  at  = gb->oam[o + 3];

                if (y < sy || y >= sy + sprite_h) continue;
                sprites_on_line++;

                int x_in = x - sx;
                if (x_in < 0 || x_in >= 8) continue;

                int y_in = y - sy;
                bool xflip = (at & 0x20) != 0;
                bool yflip = (at & 0x40) != 0;
                bool pal1  = (at & 0x10) != 0;
                bool pri_bg = (at & 0x80) != 0;

                if (yflip) y_in = sprite_h - 1 - y_in;
                int tid_use = tid;
                if (obj_16) {
                    tid_use &= 0xFE;
                    if (y_in >= 8) { tid_use += 1; y_in -= 8; }
                }

                int bit = xflip ? (x_in & 7) : (7 - (x_in & 7));

                // sprite tile fetch
                u16 tile_addr = (u16)(0x8000 + (u16)(tid_use * 16));
                u8 lo = vram_read(gb, (u16)(tile_addr + y_in * 2));
                u8 hi = vram_read(gb, (u16)(tile_addr + y_in * 2 + 1));
                u8 b0 = (lo >> bit) & 1;
                u8 b1 = (hi >> bit) & 1;
                u8 s_col = (u8)((b1 << 1) | b0);

                if (s_col == 0) continue; // transparent

                if (pri_bg && bg_col != 0) continue;

                u32 s_out = shade_from_palette(pal1 ? obp1 : obp0, s_col);
                out = s_out;
                break;
            }
        }

        gb->ppu.framebuffer[y * 160 + x] = out;
    }
}

void ppu_reset(PPU* p) {
    memset(p, 0, sizeof(*p));
    p->mode = 2;
    p->prev_mode = 2;
    p->prev_lyc = false;
    // clear to white
    for (int i = 0; i < 160 * 144; i++) p->framebuffer[i] = 0xFFFFFFFFu;
}

static void stat_update_and_interrupts(GB* gb, u8 new_mode) {
    u8 stat = gb->io[0x41];

    bool lyc = (gb->ppu.ly == gb->io[0x45]);
    if (lyc) stat |= 0x04; else stat &= (u8)~0x04;

    stat = (u8)((stat & 0xFC) | (new_mode & 0x03));
    gb->io[0x41] = stat;

    if (lyc && !gb->ppu.prev_lyc && (stat & 0x40)) {
        gb_request_interrupt(gb, INT_STAT);
    }
    gb->ppu.prev_lyc = lyc;

    if (new_mode != gb->ppu.prev_mode) {
        bool req = false;
        if (new_mode == 0 && (stat & 0x08)) req = true; // HBlank
        if (new_mode == 1 && (stat & 0x10)) req = true; // VBlank
        if (new_mode == 2 && (stat & 0x20)) req = true; // OAM
        if (req) gb_request_interrupt(gb, INT_STAT);
        gb->ppu.prev_mode = new_mode;
    }
}

void ppu_tick(struct GB* gb, int cycles) {
    u8 lcdc = gb->io[0x40];
    bool lcd_on = (lcdc & 0x80) != 0;

    if (!lcd_on) {
        gb->ppu.dot = 0;
        gb->ppu.ly = 0;
        gb->io[0x44] = 0;
        gb->ppu.mode = 0;
        stat_update_and_interrupts(gb, 0);
        return;
    }

    gb->ppu.dot += cycles;

    while (gb->ppu.dot >= 456) {
        gb->ppu.dot -= 456;

        gb->ppu.ly++;
        if (gb->ppu.ly == 144) {
            gb_request_interrupt(gb, INT_VBLANK);
        }
        if (gb->ppu.ly > 153) {
            gb->ppu.ly = 0;
        }
        gb->io[0x44] = gb->ppu.ly;
    }

    u8 new_mode;
    if (gb->ppu.ly >= 144) {
        new_mode = 1;
    } else if (gb->ppu.dot < 80) {
        new_mode = 2;
    } else if (gb->ppu.dot < 252) {
        new_mode = 3;
    } else {
        new_mode = 0;
    }

    if (gb->ppu.mode != 0 && new_mode == 0) {
        render_scanline(gb);
    }

    gb->ppu.mode = new_mode;
    stat_update_and_interrupts(gb, new_mode);
}
