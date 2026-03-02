#include "timer.h"
#include "gb.h"

void timer_reset(Timer* t) {
    t->div_counter = 0;
    t->tima_counter = 0;
}

static int tima_period_cycles(u8 tac) {
    switch (tac & 0x03) {
        case 0: return 1024; 
        case 1: return 16;   
        case 2: return 64;  
        case 3: return 256;  
    }
    return 1024;
}

void timer_tick(GB* gb, int cycles) {
    gb->timer.div_counter = (u16)(gb->timer.div_counter + cycles);
    while (gb->timer.div_counter >= 256) {
        gb->timer.div_counter -= 256;
        gb->io[0x04] = (u8)(gb->io[0x04] + 1);
    }

    u8 tac = gb->io[0x07];
    bool enabled = (tac & 0x04) != 0;
    if (!enabled) return;

    int period = tima_period_cycles(tac);
    gb->timer.tima_counter = (u16)(gb->timer.tima_counter + cycles);

    while (gb->timer.tima_counter >= (u16)period) {
        gb->timer.tima_counter -= (u16)period;
        u8 tima = gb->io[0x05];
        tima++;
        if (tima == 0x00) {
            // overflow
            gb->io[0x05] = gb->io[0x06]; 
            gb_request_interrupt(gb, INT_TIMER);
        } else {
            gb->io[0x05] = tima;
        }
    }
}
