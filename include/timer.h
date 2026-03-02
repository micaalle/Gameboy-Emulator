#pragma once
#include "gb_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Timer {
    u16 div_counter;   
    u16 tima_counter;  
} Timer;

void timer_reset(Timer* t);
void timer_tick(struct GB* gb, int cycles);

#ifdef __cplusplus
}
#endif
