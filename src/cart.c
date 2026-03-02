#include "cart.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static size_t file_size(FILE* f) {
    long cur = ftell(f);
    fseek(f, 0, SEEK_END);
    long end = ftell(f);
    fseek(f, cur, SEEK_SET);
    return (size_t)end;
}

static int rom_banks_from_code(u8 code) {
    switch (code) {
        case 0x00: return 2;
        case 0x01: return 4;
        case 0x02: return 8;
        case 0x03: return 16;
        case 0x04: return 32;
        case 0x05: return 64;
        case 0x06: return 128;
        case 0x07: return 256;
        case 0x08: return 512;
        case 0x52: return 72;
        case 0x53: return 80;
        case 0x54: return 96;
        default: return 2;
    }
}

static size_t ram_size_from_code(u8 code) {
    switch (code) {
        case 0x00: return 0;
        case 0x01: return 2 * 1024;
        case 0x02: return 8 * 1024;
        case 0x03: return 32 * 1024;
        case 0x04: return 128 * 1024;
        case 0x05: return 64 * 1024;
        default: return 0;
    }
}

static bool file_exists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static void make_save_path(char out[512], const char* rom_path) {
    strncpy(out, rom_path, 511);
    out[511] = 0;

    size_t n = strlen(out);
    for (size_t i = n; i > 0; i--) {
        if (out[i - 1] == '.') {
            out[i - 1] = 0;
            break;
        }
        if (out[i - 1] == '/' || out[i - 1] == '\\') break;
    }
    strncat(out, ".sav", 511 - strlen(out));
}

bool cart_load(Cart* c, const char* path) {
    memset(c, 0, sizeof(*c));
    FILE* f = fopen(path, "rb");
    if (!f) return false;

    size_t sz = file_size(f);
    c->rom = (u8*)xmalloc(sz);
    c->rom_size = sz;
    fread(c->rom, 1, sz, f);
    fclose(f);

    if (sz >= 0x0150) {
        memcpy(c->title, &c->rom[0x0134], 16);
        c->title[16] = 0;
        for (int i = 0; i < 16; i++) {
            if (c->title[i] == '\0') break;
            if ((unsigned char)c->title[i] < 32 || (unsigned char)c->title[i] > 126) c->title[i] = ' ';
        }
        for (int i = 15; i >= 0; i--) {
            if (c->title[i] == ' ') c->title[i] = 0;
            else break;
        }

        c->cart_type = c->rom[0x0147];
        c->rom_banks = rom_banks_from_code(c->rom[0x0148]);
        c->ram_size  = ram_size_from_code(c->rom[0x0149]);
    } else {
        strcpy(c->title, "UNKNOWN");
        c->cart_type = 0x00;
        c->rom_banks = 2;
        c->ram_size  = 0;
    }

    c->rom_bank_low5 = 1;
    c->bank_hi2 = 0;
    c->banking_mode = 0;
    c->ram_enabled = false;


    c->mbc3_rom_bank = 1;
    c->mbc3_ram_bank = 0;
    c->mbc3_rtc_sel  = 0xFF; 
    c->mbc3_latch_prev = 0;

    if (c->ram_size) {
        c->ram = (u8*)xcalloc(1, c->ram_size);
        make_save_path(c->save_path, path);

        if (file_exists(c->save_path)) {
            FILE* sf = fopen(c->save_path, "rb");
            if (sf) {
                size_t ss = file_size(sf);
                if (ss > c->ram_size) ss = c->ram_size;
                fread(c->ram, 1, ss, sf);
                fclose(sf);
            }
        }
    } else {
        c->ram = NULL;
        c->save_path[0] = 0;
    }

    return true;
}

void cart_free(Cart* c) {
    if (c->ram && c->save_path[0]) {
        FILE* sf = fopen(c->save_path, "wb");
        if (sf) {
            fwrite(c->ram, 1, c->ram_size, sf);
            fclose(sf);
        }
    }

    if (c->rom) { free(c->rom); c->rom = NULL; }
    if (c->ram) { free(c->ram); c->ram = NULL; }
    c->rom_size = 0;
    c->ram_size = 0;
}

static inline bool mbc1_like(const Cart* c) {
    return (c->cart_type == 0x01 || c->cart_type == 0x02 || c->cart_type == 0x03);
}

static inline bool mbc3_like(const Cart* c) {
    return (c->cart_type == 0x0F || c->cart_type == 0x10 ||
            c->cart_type == 0x11 || c->cart_type == 0x12 || c->cart_type == 0x13);
}

static inline int mbc1_bank0(const Cart* c) {
    if (mbc1_like(c) && c->banking_mode == 1) return (c->bank_hi2 << 5);
    return 0;
}
static inline int mbc1_bankx(const Cart* c) {
    int low = c->rom_bank_low5 & 0x1F;
    if (low == 0) low = 1;
    if (!mbc1_like(c)) return low;
    if (c->banking_mode == 1) return low;
    return (c->bank_hi2 << 5) | low;
}
static inline int mbc1_rambank(const Cart* c) {
    if (mbc1_like(c) && c->banking_mode == 1) return (c->bank_hi2 & 0x03);
    return 0;
}

u8 cart_read_rom(Cart* c, u16 addr) {
    int bank = 0;
    u32 offset = 0;

    if (mbc3_like(c)) {
        if (addr < 0x4000) {
            bank = 0;
            offset = (u32)addr;
        } else {
            bank = (int)(c->mbc3_rom_bank & 0x7F);
            if (bank == 0) bank = 1;
            offset = (u32)bank * 0x4000u + (u32)(addr - 0x4000);
        }
    } else {
        if (addr < 0x4000) {
            bank = mbc1_bank0(c);
            offset = (u32)bank * 0x4000u + addr;
        } else {
            bank = mbc1_bankx(c);
            offset = (u32)bank * 0x4000u + (u32)(addr - 0x4000);
        }
    }

    int max_banks = (int)((c->rom_size + 0x3FFF) / 0x4000);
    if (max_banks <= 0) max_banks = 1;
    bank %= max_banks;
    offset %= (u32)c->rom_size;

    if (offset < c->rom_size) return c->rom[offset];
    return 0xFF;
}

void cart_write_rom(Cart* c, u16 addr, u8 v) {
    if (mbc1_like(c)) {
        if (addr <= 0x1FFF) {
            c->ram_enabled = ((v & 0x0F) == 0x0A);
            return;
        }
        if (addr >= 0x2000 && addr <= 0x3FFF) {
            c->rom_bank_low5 = (u8)(v & 0x1F);
            if ((c->rom_bank_low5 & 0x1F) == 0) c->rom_bank_low5 = 1;
            return;
        }
        if (addr >= 0x4000 && addr <= 0x5FFF) {
            c->bank_hi2 = (u8)(v & 0x03);
            return;
        }
        if (addr >= 0x6000 && addr <= 0x7FFF) {
            c->banking_mode = (u8)(v & 0x01);
            return;
        }
        return;
    }

    if (mbc3_like(c)) {
        if (addr <= 0x1FFF) {
            c->ram_enabled = ((v & 0x0F) == 0x0A);
            return;
        }
        if (addr >= 0x2000 && addr <= 0x3FFF) {
            u8 b = (u8)(v & 0x7F);
            if (b == 0) b = 1;
            c->mbc3_rom_bank = b;
            return;
        }
        if (addr >= 0x4000 && addr <= 0x5FFF) {
            if (v <= 0x03) {
                c->mbc3_ram_bank = v;
                c->mbc3_rtc_sel = 0xFF; 
            } else if (v >= 0x08 && v <= 0x0C) {
                c->mbc3_rtc_sel = v; 
            }
            return;
        }
        if (addr >= 0x6000 && addr <= 0x7FFF) {
            c->mbc3_latch_prev = v;
            return;
        }
    }
}

u8 cart_read_ram(Cart* c, u16 addr) {
    if (!c->ram || c->ram_size == 0) return 0xFF;
    if (!c->ram_enabled && (mbc1_like(c) || mbc3_like(c))) return 0xFF;

    if (mbc3_like(c)) {
        if (c->mbc3_rtc_sel != 0xFF) return 0xFF;
        int bank = (int)(c->mbc3_ram_bank & 0x03);
        u32 offset = (u32)bank * 0x2000u + (u32)(addr - 0xA000);
        if (offset < c->ram_size) return c->ram[offset];
        return 0xFF;
    }

    int bank = mbc1_rambank(c);
    u32 offset = (u32)bank * 0x2000u + (u32)(addr - 0xA000);
    if (offset < c->ram_size) return c->ram[offset];
    return 0xFF;
}

void cart_write_ram(Cart* c, u16 addr, u8 v) {
    if (!c->ram || c->ram_size == 0) return;
    if (!c->ram_enabled && (mbc1_like(c) || mbc3_like(c))) return;

    if (mbc3_like(c)) {
        if (c->mbc3_rtc_sel != 0xFF) return; 
        int bank = (int)(c->mbc3_ram_bank & 0x03);
        u32 offset = (u32)bank * 0x2000u + (u32)(addr - 0xA000);
        if (offset < c->ram_size) c->ram[offset] = v;
        return;
    }

    int bank = mbc1_rambank(c);
    u32 offset = (u32)bank * 0x2000u + (u32)(addr - 0xA000);
    if (offset < c->ram_size) c->ram[offset] = v;
}
