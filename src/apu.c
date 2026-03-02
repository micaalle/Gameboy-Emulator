#include "apu.h"
#include "gb.h"

void apu_reset(APU* a) {
    a->dummy = 0;
}

void apu_tick(struct GB* gb, int cycles) {
    (void)gb;
    (void)cycles;
    // Audio not implemented (silent).
}
