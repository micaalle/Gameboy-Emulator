#pragma once
#include "gb_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GB GB;

typedef struct CPU {
    // 8-bit
    u8 a, f;
    u8 b, c;
    u8 d, e;
    u8 h, l;

    // 16-bit
    u16 sp;
    u16 pc;

    bool ime;

    // HALT
    bool halted;
    bool halt_bug; 

    int last_cycles;
} CPU;

void cpu_reset(CPU* cpu);
int  cpu_step(GB* gb); // returns cycles

#ifdef __cplusplus
}
#endif
