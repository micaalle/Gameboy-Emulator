#include "cpu.h"
#include "gb.h"
#include "mmu.h"
#include <string.h>

#define FLAG_Z 0x80
#define FLAG_N 0x40
#define FLAG_H 0x20
#define FLAG_C 0x10

static inline u16 reg_bc(CPU* c) { return u16_lohi(c->c, c->b); }
static inline u16 reg_de(CPU* c) { return u16_lohi(c->e, c->d); }
static inline u16 reg_hl(CPU* c) { return u16_lohi(c->l, c->h); }
static inline u16 reg_af(CPU* c) { return u16_lohi(c->f, c->a); }

static inline void set_bc(CPU* c, u16 v){ c->b = hi8(v); c->c = lo8(v); }
static inline void set_de(CPU* c, u16 v){ c->d = hi8(v); c->e = lo8(v); }
static inline void set_hl(CPU* c, u16 v){ c->h = hi8(v); c->l = lo8(v); }
static inline void set_af(CPU* c, u16 v){ c->a = hi8(v); c->f = (u8)(lo8(v) & 0xF0); }

static inline void set_flag(CPU* c, u8 flag, bool on){
    if (on) c->f |= flag;
    else c->f &= (u8)~flag;
    c->f &= 0xF0;
}
static inline bool get_flag(CPU* c, u8 flag){ return (c->f & flag) != 0; }

static inline u8 fetch8(GB* gb){
    CPU* c = &gb->cpu;
    u8 v = mmu_read8(gb, c->pc);
    if (c->halt_bug) {
        c->halt_bug = false; // PC not incremented this time
    } else {
        c->pc++;
    }
    return v;
}
static inline u16 fetch16(GB* gb){
    u8 lo = fetch8(gb);
    u8 hi = fetch8(gb);
    return u16_lohi(lo, hi);
}

static inline void push16(GB* gb, u16 v){
    CPU* c = &gb->cpu;
    c->sp--;
    mmu_write8(gb, c->sp, hi8(v));
    c->sp--;
    mmu_write8(gb, c->sp, lo8(v));
}
static inline u16 pop16(GB* gb){
    CPU* c = &gb->cpu;
    u8 lo = mmu_read8(gb, c->sp); c->sp++;
    u8 hi = mmu_read8(gb, c->sp); c->sp++;
    return u16_lohi(lo, hi);
}

static inline u8 read_r8(GB* gb, int idx){
    CPU* c = &gb->cpu;
    switch (idx & 7) {
        case 0: return c->b;
        case 1: return c->c;
        case 2: return c->d;
        case 3: return c->e;
        case 4: return c->h;
        case 5: return c->l;
        case 6: return mmu_read8(gb, reg_hl(c));
        case 7: return c->a;
    }
    return 0xFF;
}
static inline void write_r8(GB* gb, int idx, u8 v){
    CPU* c = &gb->cpu;
    switch (idx & 7) {
        case 0: c->b = v; break;
        case 1: c->c = v; break;
        case 2: c->d = v; break;
        case 3: c->e = v; break;
        case 4: c->h = v; break;
        case 5: c->l = v; break;
        case 6: mmu_write8(gb, reg_hl(c), v); break;
        case 7: c->a = v; break;
    }
}

static inline void alu_add_a(CPU* c, u8 v){
    u16 a = c->a;
    u16 r = a + v;
    set_flag(c, FLAG_Z, ((u8)r) == 0);
    set_flag(c, FLAG_N, false);
    set_flag(c, FLAG_H, ((a & 0xF) + (v & 0xF)) > 0xF);
    set_flag(c, FLAG_C, r > 0xFF);
    c->a = (u8)r;
}
static inline void alu_adc_a(CPU* c, u8 v){
    u16 a = c->a;
    u16 carry = get_flag(c, FLAG_C) ? 1 : 0;
    u16 r = a + v + carry;
    set_flag(c, FLAG_Z, ((u8)r) == 0);
    set_flag(c, FLAG_N, false);
    set_flag(c, FLAG_H, ((a & 0xF) + (v & 0xF) + carry) > 0xF);
    set_flag(c, FLAG_C, r > 0xFF);
    c->a = (u8)r;
}
static inline void alu_sub_a(CPU* c, u8 v){
    u16 a = c->a;
    u16 r = a - v;
    set_flag(c, FLAG_Z, ((u8)r) == 0);
    set_flag(c, FLAG_N, true);
    set_flag(c, FLAG_H, (a & 0xF) < (v & 0xF));
    set_flag(c, FLAG_C, a < v);
    c->a = (u8)r;
}
static inline void alu_sbc_a(CPU* c, u8 v){
    u16 a = c->a;
    u16 carry = get_flag(c, FLAG_C) ? 1 : 0;
    u16 r = a - v - carry;
    set_flag(c, FLAG_Z, ((u8)r) == 0);
    set_flag(c, FLAG_N, true);
    set_flag(c, FLAG_H, (a & 0xF) < ((v & 0xF) + carry));
    set_flag(c, FLAG_C, a < (u16)(v + carry));
    c->a = (u8)r;
}
static inline void alu_and_a(CPU* c, u8 v){
    c->a &= v;
    set_flag(c, FLAG_Z, c->a == 0);
    set_flag(c, FLAG_N, false);
    set_flag(c, FLAG_H, true);
    set_flag(c, FLAG_C, false);
}
static inline void alu_or_a(CPU* c, u8 v){
    c->a |= v;
    set_flag(c, FLAG_Z, c->a == 0);
    set_flag(c, FLAG_N, false);
    set_flag(c, FLAG_H, false);
    set_flag(c, FLAG_C, false);
}
static inline void alu_xor_a(CPU* c, u8 v){
    c->a ^= v;
    set_flag(c, FLAG_Z, c->a == 0);
    set_flag(c, FLAG_N, false);
    set_flag(c, FLAG_H, false);
    set_flag(c, FLAG_C, false);
}
static inline void alu_cp_a(CPU* c, u8 v){
    u16 a = c->a;
    u16 r = a - v;
    set_flag(c, FLAG_Z, ((u8)r) == 0);
    set_flag(c, FLAG_N, true);
    set_flag(c, FLAG_H, (a & 0xF) < (v & 0xF));
    set_flag(c, FLAG_C, a < v);
}

static inline u8 inc8(CPU* c, u8 v){
    u8 r = (u8)(v + 1);
    set_flag(c, FLAG_Z, r == 0);
    set_flag(c, FLAG_N, false);
    set_flag(c, FLAG_H, ((v & 0x0F) + 1) > 0x0F);
    return r;
}
static inline u8 dec8(CPU* c, u8 v){
    u8 r = (u8)(v - 1);
    set_flag(c, FLAG_Z, r == 0);
    set_flag(c, FLAG_N, true);
    set_flag(c, FLAG_H, (v & 0x0F) == 0);
    return r;
}

static inline void add_hl(CPU* c, u16 v){
    u32 hl = reg_hl(c);
    u32 r = hl + v;
    set_flag(c, FLAG_N, false);
    set_flag(c, FLAG_H, ((hl & 0x0FFF) + (v & 0x0FFF)) > 0x0FFF);
    set_flag(c, FLAG_C, r > 0xFFFF);
    set_hl(c, (u16)r);
}

static inline void daa(CPU* c){
    u8 a = c->a;
    int adj = 0;
    bool carry = get_flag(c, FLAG_C);

    if (!get_flag(c, FLAG_N)) {
        if (get_flag(c, FLAG_H) || (a & 0x0F) > 9) adj |= 0x06;
        if (carry || a > 0x99) { adj |= 0x60; carry = true; }
        a = (u8)(a + adj);
    } else {
        if (get_flag(c, FLAG_H)) adj |= 0x06;
        if (carry) adj |= 0x60;
        a = (u8)(a - adj);
    }

    c->a = a;
    set_flag(c, FLAG_Z, c->a == 0);
    set_flag(c, FLAG_H, false);
    set_flag(c, FLAG_C, carry);
}

static inline u8 rlca(CPU* c){
    u8 a = c->a;
    u8 out = (u8)((a << 1) | (a >> 7));
    set_flag(c, FLAG_Z, false);
    set_flag(c, FLAG_N, false);
    set_flag(c, FLAG_H, false);
    set_flag(c, FLAG_C, (a >> 7) & 1);
    c->a = out;
    return out;
}
static inline u8 rrca(CPU* c){
    u8 a = c->a;
    u8 out = (u8)((a >> 1) | (a << 7));
    set_flag(c, FLAG_Z, false);
    set_flag(c, FLAG_N, false);
    set_flag(c, FLAG_H, false);
    set_flag(c, FLAG_C, a & 1);
    c->a = out;
    return out;
}
static inline u8 rla(CPU* c){
    u8 a = c->a;
    u8 carry = get_flag(c, FLAG_C) ? 1 : 0;
    u8 out = (u8)((a << 1) | carry);
    set_flag(c, FLAG_Z, false);
    set_flag(c, FLAG_N, false);
    set_flag(c, FLAG_H, false);
    set_flag(c, FLAG_C, (a >> 7) & 1);
    c->a = out;
    return out;
}
static inline u8 rra(CPU* c){
    u8 a = c->a;
    u8 carry = get_flag(c, FLAG_C) ? 0x80 : 0;
    u8 out = (u8)((a >> 1) | carry);
    set_flag(c, FLAG_Z, false);
    set_flag(c, FLAG_N, false);
    set_flag(c, FLAG_H, false);
    set_flag(c, FLAG_C, a & 1);
    c->a = out;
    return out;
}

static inline u8 op_rlc(CPU* c, u8 v){
    u8 out = (u8)((v << 1) | (v >> 7));
    set_flag(c, FLAG_Z, out == 0);
    set_flag(c, FLAG_N, false);
    set_flag(c, FLAG_H, false);
    set_flag(c, FLAG_C, (v >> 7) & 1);
    return out;
}
static inline u8 op_rrc(CPU* c, u8 v){
    u8 out = (u8)((v >> 1) | (v << 7));
    set_flag(c, FLAG_Z, out == 0);
    set_flag(c, FLAG_N, false);
    set_flag(c, FLAG_H, false);
    set_flag(c, FLAG_C, v & 1);
    return out;
}
static inline u8 op_rl(CPU* c, u8 v){
    u8 carry = get_flag(c, FLAG_C) ? 1 : 0;
    u8 out = (u8)((v << 1) | carry);
    set_flag(c, FLAG_Z, out == 0);
    set_flag(c, FLAG_N, false);
    set_flag(c, FLAG_H, false);
    set_flag(c, FLAG_C, (v >> 7) & 1);
    return out;
}
static inline u8 op_rr(CPU* c, u8 v){
    u8 carry = get_flag(c, FLAG_C) ? 0x80 : 0;
    u8 out = (u8)((v >> 1) | carry);
    set_flag(c, FLAG_Z, out == 0);
    set_flag(c, FLAG_N, false);
    set_flag(c, FLAG_H, false);
    set_flag(c, FLAG_C, v & 1);
    return out;
}
static inline u8 op_sla(CPU* c, u8 v){
    set_flag(c, FLAG_C, (v >> 7) & 1);
    u8 out = (u8)(v << 1);
    set_flag(c, FLAG_Z, out == 0);
    set_flag(c, FLAG_N, false);
    set_flag(c, FLAG_H, false);
    return out;
}
static inline u8 op_sra(CPU* c, u8 v){
    set_flag(c, FLAG_C, v & 1);
    u8 out = (u8)((v >> 1) | (v & 0x80));
    set_flag(c, FLAG_Z, out == 0);
    set_flag(c, FLAG_N, false);
    set_flag(c, FLAG_H, false);
    return out;
}
static inline u8 op_swap(CPU* c, u8 v){
    u8 out = (u8)((v << 4) | (v >> 4));
    set_flag(c, FLAG_Z, out == 0);
    set_flag(c, FLAG_N, false);
    set_flag(c, FLAG_H, false);
    set_flag(c, FLAG_C, false);
    return out;
}
static inline u8 op_srl(CPU* c, u8 v){
    set_flag(c, FLAG_C, v & 1);
    u8 out = (u8)(v >> 1);
    set_flag(c, FLAG_Z, out == 0);
    set_flag(c, FLAG_N, false);
    set_flag(c, FLAG_H, false);
    return out;
}

static int exec_cb(GB* gb, u8 cb) {
    CPU* c = &gb->cpu;

    int reg = cb & 0x07;
    int op  = (cb >> 3) & 0x07;
    int grp = (cb >> 6) & 0x03;

    bool is_hl = (reg == 6);
    int cycles = is_hl ? 16 : 8;

    if (grp == 0) {
        u8 v = read_r8(gb, reg);
        switch (op) {
            case 0: v = op_rlc(c, v); break;
            case 1: v = op_rrc(c, v); break;
            case 2: v = op_rl(c, v);  break;
            case 3: v = op_rr(c, v);  break;
            case 4: v = op_sla(c, v); break;
            case 5: v = op_sra(c, v); break;
            case 6: v = op_swap(c, v);break;
            case 7: v = op_srl(c, v); break;
        }
        write_r8(gb, reg, v);
        return cycles;
    } else if (grp == 1) {
        // BIT b,r
        int bit = (cb >> 3) & 7;
        u8 v = read_r8(gb, reg);
        set_flag(c, FLAG_Z, ((v >> bit) & 1) == 0);
        set_flag(c, FLAG_N, false);
        set_flag(c, FLAG_H, true);
        return cycles;
    } else if (grp == 2) {
        // RES b,r
        int bit = (cb >> 3) & 7;
        u8 v = read_r8(gb, reg);
        v = (u8)(v & ~(1u << bit));
        write_r8(gb, reg, v);
        return cycles;
    } else {
        // SET b,r
        int bit = (cb >> 3) & 7;
        u8 v = read_r8(gb, reg);
        v = (u8)(v | (1u << bit));
        write_r8(gb, reg, v);
        return cycles;
    }
}

static inline bool cond_nz(CPU* c){ return !get_flag(c, FLAG_Z); }
static inline bool cond_z(CPU* c){ return get_flag(c, FLAG_Z); }
static inline bool cond_nc(CPU* c){ return !get_flag(c, FLAG_C); }
static inline bool cond_c(CPU* c){ return get_flag(c, FLAG_C); }

static int service_interrupt(GB* gb, u8 pending) {
    CPU* c = &gb->cpu;
    u16 vec = 0x0000;
    u8 bit = 0;

    if (pending & INT_VBLANK) { vec = 0x0040; bit = INT_VBLANK; }
    else if (pending & INT_STAT) { vec = 0x0048; bit = INT_STAT; }
    else if (pending & INT_TIMER) { vec = 0x0050; bit = INT_TIMER; }
    else if (pending & INT_SERIAL) { vec = 0x0058; bit = INT_SERIAL; }
    else if (pending & INT_JOYPAD) { vec = 0x0060; bit = INT_JOYPAD; }

    gb->io[0x0F] = (u8)((gb->io[0x0F] & ~bit) | 0xE0);

    c->ime = false;
    c->halted = false;

    push16(gb, c->pc);
    c->pc = vec;
    return 20; 
}

void cpu_reset(CPU* cpu) {
    memset(cpu, 0, sizeof(*cpu));

    cpu->a  = 0x01;
    cpu->f  = 0xB0;
    cpu->b  = 0x00;
    cpu->c  = 0x13;
    cpu->d  = 0x00;
    cpu->e  = 0xD8;
    cpu->h  = 0x01;
    cpu->l  = 0x4D;
    cpu->sp = 0xFFFE;
    cpu->pc = 0x0100;

    cpu->ime = false;
    cpu->halted = false;
    cpu->halt_bug = false;
    cpu->last_cycles = 0;
}

int cpu_step(GB* gb) {
    CPU* c = &gb->cpu;

    // interrupt chk
    u8 pending = (u8)((gb->io[0x0F] & gb->ie) & 0x1F);

    if (c->halted) {
        if (pending) {
            c->halted = false;
            // halt exit
            if (!c->ime) c->halt_bug = true;
        } else {
            c->last_cycles = 4;
            return 4; // idle tick
        }
    }

    if (pending && c->ime) {
        int cyc = service_interrupt(gb, pending);
        c->last_cycles = cyc;
        return cyc;
    }

    u8 op = fetch8(gb);

    if (op >= 0x40 && op <= 0x7F) {
        if (op == 0x76) {
            c->halted = true;
            c->last_cycles = 4;
            return 4;
        }
        int dst = (op >> 3) & 7;
        int src = op & 7;
        u8 v = read_r8(gb, src);
        write_r8(gb, dst, v);
        int cycles = (dst == 6 || src == 6) ? 8 : 4;
        c->last_cycles = cycles;
        return cycles;
    }
    if (op >= 0x80 && op <= 0xBF) {
        int src = op & 7;
        u8 v = read_r8(gb, src);
        int group = (op >> 3) & 7;
        switch (group) {
            case 0: alu_add_a(c, v); break;
            case 1: alu_adc_a(c, v); break;
            case 2: alu_sub_a(c, v); break;
            case 3: alu_sbc_a(c, v); break;
            case 4: alu_and_a(c, v); break;
            case 5: alu_xor_a(c, v); break;
            case 6: alu_or_a(c, v);  break;
            case 7: alu_cp_a(c, v);  break;
        }
        int cycles = (src == 6) ? 8 : 4;
        c->last_cycles = cycles;
        return cycles;
    }

    switch (op) {
        case 0x00: c->last_cycles = 4; return 4; // NOP

        case 0x01: set_bc(c, fetch16(gb)); c->last_cycles = 12; return 12;
        case 0x11: set_de(c, fetch16(gb)); c->last_cycles = 12; return 12;
        case 0x21: set_hl(c, fetch16(gb)); c->last_cycles = 12; return 12;
        case 0x31: c->sp = fetch16(gb); c->last_cycles = 12; return 12;

        case 0x02: mmu_write8(gb, reg_bc(c), c->a); c->last_cycles = 8; return 8;
        case 0x12: mmu_write8(gb, reg_de(c), c->a); c->last_cycles = 8; return 8;
        case 0x22: {
            u16 hl = reg_hl(c);
            mmu_write8(gb, hl, c->a);
            set_hl(c, (u16)(hl + 1));
            c->last_cycles = 8; return 8;
        }
        case 0x32: {
            u16 hl = reg_hl(c);
            mmu_write8(gb, hl, c->a);
            set_hl(c, (u16)(hl - 1));
            c->last_cycles = 8; return 8;
        }

        case 0x0A: c->a = mmu_read8(gb, reg_bc(c)); c->last_cycles = 8; return 8;
        case 0x1A: c->a = mmu_read8(gb, reg_de(c)); c->last_cycles = 8; return 8;
        case 0x2A: {
            u16 hl = reg_hl(c);
            c->a = mmu_read8(gb, hl);
            set_hl(c, (u16)(hl + 1));
            c->last_cycles = 8; return 8;
        }
        case 0x3A: {
            u16 hl = reg_hl(c);
            c->a = mmu_read8(gb, hl);
            set_hl(c, (u16)(hl - 1));
            c->last_cycles = 8; return 8;
        }

        case 0x03: set_bc(c, (u16)(reg_bc(c) + 1)); c->last_cycles = 8; return 8;
        case 0x13: set_de(c, (u16)(reg_de(c) + 1)); c->last_cycles = 8; return 8;
        case 0x23: set_hl(c, (u16)(reg_hl(c) + 1)); c->last_cycles = 8; return 8;
        case 0x33: c->sp++; c->last_cycles = 8; return 8;

        case 0x0B: set_bc(c, (u16)(reg_bc(c) - 1)); c->last_cycles = 8; return 8;
        case 0x1B: set_de(c, (u16)(reg_de(c) - 1)); c->last_cycles = 8; return 8;
        case 0x2B: set_hl(c, (u16)(reg_hl(c) - 1)); c->last_cycles = 8; return 8;
        case 0x3B: c->sp--; c->last_cycles = 8; return 8;

        case 0x04: c->b = inc8(c, c->b); c->last_cycles = 4; return 4;
        case 0x0C: c->c = inc8(c, c->c); c->last_cycles = 4; return 4;
        case 0x14: c->d = inc8(c, c->d); c->last_cycles = 4; return 4;
        case 0x1C: c->e = inc8(c, c->e); c->last_cycles = 4; return 4;
        case 0x24: c->h = inc8(c, c->h); c->last_cycles = 4; return 4;
        case 0x2C: c->l = inc8(c, c->l); c->last_cycles = 4; return 4;
        case 0x3C: c->a = inc8(c, c->a); c->last_cycles = 4; return 4;
        case 0x34: {
            u16 hl = reg_hl(c);
            u8 v = mmu_read8(gb, hl);
            v = inc8(c, v);
            mmu_write8(gb, hl, v);
            c->last_cycles = 12; return 12;
        }

        case 0x05: c->b = dec8(c, c->b); c->last_cycles = 4; return 4;
        case 0x0D: c->c = dec8(c, c->c); c->last_cycles = 4; return 4;
        case 0x15: c->d = dec8(c, c->d); c->last_cycles = 4; return 4;
        case 0x1D: c->e = dec8(c, c->e); c->last_cycles = 4; return 4;
        case 0x25: c->h = dec8(c, c->h); c->last_cycles = 4; return 4;
        case 0x2D: c->l = dec8(c, c->l); c->last_cycles = 4; return 4;
        case 0x3D: c->a = dec8(c, c->a); c->last_cycles = 4; return 4;
        case 0x35: {
            u16 hl = reg_hl(c);
            u8 v = mmu_read8(gb, hl);
            v = dec8(c, v);
            mmu_write8(gb, hl, v);
            c->last_cycles = 12; return 12;
        }

        case 0x06: c->b = fetch8(gb); c->last_cycles = 8; return 8;
        case 0x0E: c->c = fetch8(gb); c->last_cycles = 8; return 8;
        case 0x16: c->d = fetch8(gb); c->last_cycles = 8; return 8;
        case 0x1E: c->e = fetch8(gb); c->last_cycles = 8; return 8;
        case 0x26: c->h = fetch8(gb); c->last_cycles = 8; return 8;
        case 0x2E: c->l = fetch8(gb); c->last_cycles = 8; return 8;
        case 0x3E: c->a = fetch8(gb); c->last_cycles = 8; return 8;
        case 0x36: {
            u8 v = fetch8(gb);
            mmu_write8(gb, reg_hl(c), v);
            c->last_cycles = 12; return 12;
        }

        case 0x07: rlca(c); c->last_cycles = 4; return 4;
        case 0x0F: rrca(c); c->last_cycles = 4; return 4;
        case 0x17: rla(c);  c->last_cycles = 4; return 4;
        case 0x1F: rra(c);  c->last_cycles = 4; return 4;

        case 0x08: {
            u16 a16 = fetch16(gb);
            mmu_write16(gb, a16, c->sp);
            c->last_cycles = 20; return 20;
        }

        case 0x09: add_hl(c, reg_bc(c)); c->last_cycles = 8; return 8;
        case 0x19: add_hl(c, reg_de(c)); c->last_cycles = 8; return 8;
        case 0x29: add_hl(c, reg_hl(c)); c->last_cycles = 8; return 8;
        case 0x39: add_hl(c, c->sp);     c->last_cycles = 8; return 8;

        case 0x10: { 
            (void)fetch8(gb);
            c->last_cycles = 4; return 4;
        }

        case 0x18: { 
            s8 r = (s8)fetch8(gb);
            c->pc = (u16)(c->pc + r);
            c->last_cycles = 12; return 12;
        }
        case 0x20: { s8 r = (s8)fetch8(gb); if (cond_nz(c)) { c->pc = (u16)(c->pc + r); c->last_cycles = 12; return 12; } c->last_cycles = 8; return 8; }
        case 0x28: { s8 r = (s8)fetch8(gb); if (cond_z(c))  { c->pc = (u16)(c->pc + r); c->last_cycles = 12; return 12; } c->last_cycles = 8; return 8; }
        case 0x30: { s8 r = (s8)fetch8(gb); if (cond_nc(c)) { c->pc = (u16)(c->pc + r); c->last_cycles = 12; return 12; } c->last_cycles = 8; return 8; }
        case 0x38: { s8 r = (s8)fetch8(gb); if (cond_c(c))  { c->pc = (u16)(c->pc + r); c->last_cycles = 12; return 12; } c->last_cycles = 8; return 8; }

        case 0x27: daa(c); c->last_cycles = 4; return 4;
        case 0x2F: c->a = (u8)~c->a; set_flag(c, FLAG_N, true); set_flag(c, FLAG_H, true); c->last_cycles = 4; return 4;
        case 0x37: set_flag(c, FLAG_N, false); set_flag(c, FLAG_H, false); set_flag(c, FLAG_C, true); c->last_cycles = 4; return 4;
        case 0x3F: set_flag(c, FLAG_N, false); set_flag(c, FLAG_H, false); set_flag(c, FLAG_C, !get_flag(c, FLAG_C)); c->last_cycles = 4; return 4;

        case 0xC5: push16(gb, reg_bc(c)); c->last_cycles = 16; return 16;
        case 0xD5: push16(gb, reg_de(c)); c->last_cycles = 16; return 16;
        case 0xE5: push16(gb, reg_hl(c)); c->last_cycles = 16; return 16;
        case 0xF5: push16(gb, (u16)((c->a << 8) | (c->f & 0xF0))); c->last_cycles = 16; return 16;

        case 0xC1: set_bc(c, pop16(gb)); c->last_cycles = 12; return 12;
        case 0xD1: set_de(c, pop16(gb)); c->last_cycles = 12; return 12;
        case 0xE1: set_hl(c, pop16(gb)); c->last_cycles = 12; return 12;
        case 0xF1: {
            u16 v = pop16(gb);
            c->a = hi8(v);
            c->f = (u8)(lo8(v) & 0xF0);
            c->last_cycles = 12; return 12;
        }

        case 0xC3: c->pc = fetch16(gb); c->last_cycles = 16; return 16;
        case 0xE9: c->pc = reg_hl(c); c->last_cycles = 4; return 4;

        case 0xC2: { u16 a = fetch16(gb); if (cond_nz(c)) { c->pc = a; c->last_cycles = 16; return 16; } c->last_cycles = 12; return 12; }
        case 0xCA: { u16 a = fetch16(gb); if (cond_z(c))  { c->pc = a; c->last_cycles = 16; return 16; } c->last_cycles = 12; return 12; }
        case 0xD2: { u16 a = fetch16(gb); if (cond_nc(c)) { c->pc = a; c->last_cycles = 16; return 16; } c->last_cycles = 12; return 12; }
        case 0xDA: { u16 a = fetch16(gb); if (cond_c(c))  { c->pc = a; c->last_cycles = 16; return 16; } c->last_cycles = 12; return 12; }

        case 0xCD: { u16 a = fetch16(gb); push16(gb, c->pc); c->pc = a; c->last_cycles = 24; return 24; }
        case 0xC4: { u16 a = fetch16(gb); if (cond_nz(c)) { push16(gb, c->pc); c->pc = a; c->last_cycles = 24; return 24; } c->last_cycles = 12; return 12; }
        case 0xCC: { u16 a = fetch16(gb); if (cond_z(c))  { push16(gb, c->pc); c->pc = a; c->last_cycles = 24; return 24; } c->last_cycles = 12; return 12; }
        case 0xD4: { u16 a = fetch16(gb); if (cond_nc(c)) { push16(gb, c->pc); c->pc = a; c->last_cycles = 24; return 24; } c->last_cycles = 12; return 12; }
        case 0xDC: { u16 a = fetch16(gb); if (cond_c(c))  { push16(gb, c->pc); c->pc = a; c->last_cycles = 24; return 24; } c->last_cycles = 12; return 12; }

        case 0xC9: c->pc = pop16(gb); c->last_cycles = 16; return 16;
        case 0xC0: if (cond_nz(c)) { c->pc = pop16(gb); c->last_cycles = 20; return 20; } c->last_cycles = 8; return 8;
        case 0xC8: if (cond_z(c))  { c->pc = pop16(gb); c->last_cycles = 20; return 20; } c->last_cycles = 8; return 8;
        case 0xD0: if (cond_nc(c)) { c->pc = pop16(gb); c->last_cycles = 20; return 20; } c->last_cycles = 8; return 8;
        case 0xD8: if (cond_c(c))  { c->pc = pop16(gb); c->last_cycles = 20; return 20; } c->last_cycles = 8; return 8;

        case 0xD9: 
            c->pc = pop16(gb);
            c->ime = true;
            c->last_cycles = 16; return 16;

        case 0xC7: push16(gb, c->pc); c->pc = 0x00; c->last_cycles = 16; return 16;
        case 0xCF: push16(gb, c->pc); c->pc = 0x08; c->last_cycles = 16; return 16;
        case 0xD7: push16(gb, c->pc); c->pc = 0x10; c->last_cycles = 16; return 16;
        case 0xDF: push16(gb, c->pc); c->pc = 0x18; c->last_cycles = 16; return 16;
        case 0xE7: push16(gb, c->pc); c->pc = 0x20; c->last_cycles = 16; return 16;
        case 0xEF: push16(gb, c->pc); c->pc = 0x28; c->last_cycles = 16; return 16;
        case 0xF7: push16(gb, c->pc); c->pc = 0x30; c->last_cycles = 16; return 16;
        case 0xFF: push16(gb, c->pc); c->pc = 0x38; c->last_cycles = 16; return 16;

        case 0xF3: c->ime = false; c->last_cycles = 4; return 4;
        case 0xFB: c->ime = true;  c->last_cycles = 4; return 4;

        case 0xCB: {
            u8 cb = fetch8(gb);
            int cyc = exec_cb(gb, cb);
            c->last_cycles = cyc;
            return cyc;
        }

        case 0xE0: { 
            u16 a = (u16)(0xFF00 | fetch8(gb));
            mmu_write8(gb, a, c->a);
            c->last_cycles = 12; return 12;
        }
        case 0xF0: { 
            u16 a = (u16)(0xFF00 | fetch8(gb));
            c->a = mmu_read8(gb, a);
            c->last_cycles = 12; return 12;
        }
        case 0xE2: { 
            mmu_write8(gb, (u16)(0xFF00 | c->c), c->a);
            c->last_cycles = 8; return 8;
        }
        case 0xF2: {
            c->a = mmu_read8(gb, (u16)(0xFF00 | c->c));
            c->last_cycles = 8; return 8;
        }

        case 0xEA: { u16 a = fetch16(gb); mmu_write8(gb, a, c->a); c->last_cycles = 16; return 16; }
        case 0xFA: { u16 a = fetch16(gb); c->a = mmu_read8(gb, a); c->last_cycles = 16; return 16; }

        case 0xE8: {
            s8 r = (s8)fetch8(gb);
            u16 sp = c->sp;
            u16 res = (u16)(sp + r);
            set_flag(c, FLAG_Z, false);
            set_flag(c, FLAG_N, false);
            set_flag(c, FLAG_H, ((sp & 0xF) + ((u16)(u8)r & 0xF)) > 0xF);
            set_flag(c, FLAG_C, ((sp & 0xFF) + ((u16)(u8)r & 0xFF)) > 0xFF);
            c->sp = res;
            c->last_cycles = 16; return 16;
        }
        case 0xF8: {
            s8 r = (s8)fetch8(gb);
            u16 sp = c->sp;
            u16 res = (u16)(sp + r);
            set_flag(c, FLAG_Z, false);
            set_flag(c, FLAG_N, false);
            set_flag(c, FLAG_H, ((sp & 0xF) + ((u16)(u8)r & 0xF)) > 0xF);
            set_flag(c, FLAG_C, ((sp & 0xFF) + ((u16)(u8)r & 0xFF)) > 0xFF);
            set_hl(c, res);
            c->last_cycles = 12; return 12;
        }
        case 0xF9:
            c->sp = reg_hl(c);
            c->last_cycles = 8; return 8;

        case 0xC6: alu_add_a(c, fetch8(gb)); c->last_cycles = 8; return 8;
        case 0xCE: alu_adc_a(c, fetch8(gb)); c->last_cycles = 8; return 8;
        case 0xD6: alu_sub_a(c, fetch8(gb)); c->last_cycles = 8; return 8;
        case 0xDE: alu_sbc_a(c, fetch8(gb)); c->last_cycles = 8; return 8;
        case 0xE6: alu_and_a(c, fetch8(gb)); c->last_cycles = 8; return 8;
        case 0xEE: alu_xor_a(c, fetch8(gb)); c->last_cycles = 8; return 8;
        case 0xF6: alu_or_a(c, fetch8(gb));  c->last_cycles = 8; return 8;
        case 0xFE: alu_cp_a(c, fetch8(gb));  c->last_cycles = 8; return 8;

        default:
            c->last_cycles = 4;
            return 4;
    }
}
