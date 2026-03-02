#pragma once
#include "gb_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Cart {
    u8*  rom;
    size_t rom_size;

    u8 cart_type;
    int rom_banks;

    u8* ram;
    size_t ram_size;
    bool ram_enabled;

 
    u8 rom_bank_low5; 
    u8 bank_hi2;      
    u8 banking_mode;  

    u8 mbc3_rom_bank;  
    u8 mbc3_ram_bank;   
    u8 mbc3_rtc_sel;    
    u8 mbc3_latch_prev; 

    char title[17];
    char save_path[512];
} Cart;

bool cart_load(Cart* c, const char* path);
void cart_free(Cart* c);

u8  cart_read_rom(struct Cart* c, u16 addr);
void cart_write_rom(struct Cart* c, u16 addr, u8 v);

u8  cart_read_ram(struct Cart* c, u16 addr);
void cart_write_ram(struct Cart* c, u16 addr, u8 v);

#ifdef __cplusplus
}
#endif
