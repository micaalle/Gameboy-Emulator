#pragma once
#include "gb_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APU_SAMPLE_RATE 44100
#define APU_BUFFER_FRAMES 4096

struct GB;

// volume implementation 
typedef struct APU {
    int sample_accum;
    s16 samples[APU_BUFFER_FRAMES * 2]; 
    int sample_frames;

    double square_phase[2];
    double wave_phase;
    double noise_phase;
    u16 noise_lfsr;

    bool channel_enabled[4];

    int frame_seq_accum;
    int frame_seq_step;

    int length_counter[4];
    bool length_enabled[4];

    u8 current_volume[4];
    u8 env_period[4];
    int env_timer[4];
    int env_dir[4];
    bool env_active[4];

    double hp_last_in_l;
    double hp_last_in_r;
    double hp_last_out_l;
    double hp_last_out_r;
} APU;

void apu_reset(APU* a);
void apu_tick(struct GB* gb, int cycles);
void apu_io_write(struct GB* gb, u8 idx, u8 value);

int apu_samples_available(const APU* a);
const s16* apu_samples_data(const APU* a);
void apu_clear_samples(APU* a);

#ifdef __cplusplus
}
#endif
