#include "apu.h"
#include "gb.h"
#include <string.h>

#define GB_CPU_HZ 4194304
#define FRAME_SEQ_CYCLES 8192 // 512 Hz

static double duty_ratio(u8 duty_id) {
    switch (duty_id & 3) {
        case 0: return 0.125;
        case 1: return 0.250;
        case 2: return 0.500;
        default: return 0.750;
    }
}

static double square_freq(u8 lo, u8 hi) {
    int f = ((hi & 0x07) << 8) | lo;
    if (f >= 2048) return 0.0;
    return 131072.0 / (double)(2048 - f);
}

static double wave_freq(u8 lo, u8 hi) {
    int f = ((hi & 0x07) << 8) | lo;
    if (f >= 2048) return 0.0;
    return 65536.0 / (double)(2048 - f);
}

static void setup_envelope(APU* a, int ch, u8 env) {
    a->current_volume[ch] = (u8)(env >> 4);
    a->env_period[ch] = (u8)(env & 0x07);
    a->env_dir[ch] = (env & 0x08) ? 1 : -1;
    a->env_timer[ch] = a->env_period[ch] ? a->env_period[ch] : 8;
    a->env_active[ch] = a->env_period[ch] != 0;
}

static void trigger_square(GB* gb, int ch) {
    APU* a = &gb->apu;
    u8 len = gb->io[ch == 0 ? 0x11 : 0x16] & 0x3F;
    u8 env = gb->io[ch == 0 ? 0x12 : 0x17];

    a->channel_enabled[ch] = (env & 0xF8) != 0;
    a->square_phase[ch] = 0.0;
    setup_envelope(a, ch, env);

    if (a->length_counter[ch] == 0) {
        a->length_counter[ch] = len ? (64 - len) : 64;
    }
}

static void trigger_wave(GB* gb) {
    APU* a = &gb->apu;
    a->channel_enabled[2] = (gb->io[0x1A] & 0x80) != 0;
    a->wave_phase = 0.0;

    if (a->length_counter[2] == 0) {
        u8 len = gb->io[0x1B];
        a->length_counter[2] = len ? (256 - len) : 256;
    }
}

static void trigger_noise(GB* gb) {
    APU* a = &gb->apu;
    u8 env = gb->io[0x21];
    u8 len = gb->io[0x20] & 0x3F;

    a->channel_enabled[3] = (env & 0xF8) != 0;
    a->noise_lfsr = 0x7FFF;
    a->noise_phase = 0.0;
    setup_envelope(a, 3, env);

    if (a->length_counter[3] == 0) {
        a->length_counter[3] = len ? (64 - len) : 64;
    }
}

void apu_reset(APU* a) {
    memset(a, 0, sizeof(*a));
    a->noise_lfsr = 0x7FFF;
}

void apu_io_write(GB* gb, u8 idx, u8 value) {
    APU* a = &gb->apu;

    if (idx == 0x26) { // NR52 
        if (!(value & 0x80)) {
            for (int i = 0; i < 4; i++) a->channel_enabled[i] = false;
        }
        return;
    }

    if (!(gb->io[0x26] & 0x80)) return;

    switch (idx) {
        case 0x11: // NR11 
            a->length_counter[0] = (value & 0x3F) ? (64 - (value & 0x3F)) : 64;
            break;
        case 0x12: // NR12
            if ((value & 0xF8) == 0) a->channel_enabled[0] = false;
            break;
        case 0x14: // NR14 
            a->length_enabled[0] = (value & 0x40) != 0;
            if (value & 0x80) trigger_square(gb, 0);
            break;

        case 0x16: // NR21 
            a->length_counter[1] = (value & 0x3F) ? (64 - (value & 0x3F)) : 64;
            break;
        case 0x17: // NR22 
            if ((value & 0xF8) == 0) a->channel_enabled[1] = false;
            break;
        case 0x19: // NR24 
            a->length_enabled[1] = (value & 0x40) != 0;
            if (value & 0x80) trigger_square(gb, 1);
            break;

        case 0x1B: // NR31 
            a->length_counter[2] = value ? (256 - value) : 256;
            break;
        case 0x1E: // NR34 
            a->length_enabled[2] = (value & 0x40) != 0;
            if (value & 0x80) trigger_wave(gb);
            break;

        case 0x20: // NR41 
            a->length_counter[3] = (value & 0x3F) ? (64 - (value & 0x3F)) : 64;
            break;
        case 0x21: // NR42 
            if ((value & 0xF8) == 0) a->channel_enabled[3] = false;
            break;
        case 0x23: // NR44
            a->length_enabled[3] = (value & 0x40) != 0;
            if (value & 0x80) trigger_noise(gb);
            break;

        default:
            break;
    }
}

static double square_sample(GB* gb, int ch) {
    APU* a = &gb->apu;
    if (!a->channel_enabled[ch]) return 0.0;

    u8 duty_reg = gb->io[ch == 0 ? 0x11 : 0x16];
    u8 env      = gb->io[ch == 0 ? 0x12 : 0x17];
    u8 lo       = gb->io[ch == 0 ? 0x13 : 0x18];
    u8 hi       = gb->io[ch == 0 ? 0x14 : 0x19];

    if ((env & 0xF8) == 0) {
        a->channel_enabled[ch] = false;
        return 0.0;
    }

    double freq = square_freq(lo, hi);
    double vol = (double)a->current_volume[ch] / 15.0;
    double duty = duty_ratio(duty_reg >> 6);
    double s = (a->square_phase[ch] < duty) ? 1.0 : -1.0;

    a->square_phase[ch] += freq / (double)APU_SAMPLE_RATE;
    while (a->square_phase[ch] >= 1.0) a->square_phase[ch] -= 1.0;

    return s * vol;
}

static double wave_sample(GB* gb) {
    APU* a = &gb->apu;
    if (!a->channel_enabled[2]) return 0.0;
    if (!(gb->io[0x1A] & 0x80)) return 0.0;

    int level = (gb->io[0x1C] >> 5) & 3;
    if (level == 0) return 0.0;

    int sample_index = ((int)(a->wave_phase * 32.0)) & 31;
    u8 b = gb->io[0x30 + sample_index / 2];
    int nibble = (sample_index & 1) ? (b & 0x0F) : (b >> 4);

    double s = ((double)nibble / 7.5) - 1.0;
    if (level == 2) s *= 0.5;
    if (level == 3) s *= 0.25;

    double freq = wave_freq(gb->io[0x1D], gb->io[0x1E]);
    a->wave_phase += freq / (double)APU_SAMPLE_RATE;
    while (a->wave_phase >= 1.0) a->wave_phase -= 1.0;

    return s;
}

static void noise_step(APU* a, bool width7) {
    u16 x = (u16)((a->noise_lfsr ^ (a->noise_lfsr >> 1)) & 1);
    a->noise_lfsr = (u16)((a->noise_lfsr >> 1) | (x << 14));
    if (width7) {
        a->noise_lfsr = (u16)((a->noise_lfsr & ~(1u << 6)) | (x << 6));
    }
}

static double noise_sample(GB* gb) {
    APU* a = &gb->apu;
    if (!a->channel_enabled[3]) return 0.0;

    u8 env = gb->io[0x21];
    u8 poly = gb->io[0x22];
    if ((env & 0xF8) == 0) {
        a->channel_enabled[3] = false;
        return 0.0;
    }

    int divisor_code = poly & 7;
    int divisor = divisor_code == 0 ? 8 : divisor_code * 16;
    int shift = (poly >> 4) & 0x0F;
    bool width7 = (poly & 0x08) != 0;

    double freq = 4194304.0 / (double)(divisor << shift);
    a->noise_phase += freq / (double)APU_SAMPLE_RATE;
    while (a->noise_phase >= 1.0) {
        noise_step(a, width7);
        a->noise_phase -= 1.0;
    }

    double vol = (double)a->current_volume[3] / 15.0;
    double s = (a->noise_lfsr & 1) ? -1.0 : 1.0;
    return s * vol;
}

static void push_stereo(APU* a, double left, double right) {
    if (a->sample_frames >= APU_BUFFER_FRAMES) return;

    // dc blocking for sound quality
    const double hp = 0.995;
    double filtered_l = left - a->hp_last_in_l + hp * a->hp_last_out_l;
    double filtered_r = right - a->hp_last_in_r + hp * a->hp_last_out_r;
    a->hp_last_in_l = left;
    a->hp_last_in_r = right;
    a->hp_last_out_l = filtered_l;
    a->hp_last_out_r = filtered_r;
    left = filtered_l;
    right = filtered_r;

    if (left > 1.0) left = 1.0;
    if (left < -1.0) left = -1.0;
    if (right > 1.0) right = 1.0;
    if (right < -1.0) right = -1.0;

    int i = a->sample_frames * 2;
    a->samples[i + 0] = (s16)(left * 22000.0);
    a->samples[i + 1] = (s16)(right * 22000.0);
    a->sample_frames++;
}

static void render_sample(GB* gb) {
    APU* a = &gb->apu;

    if (!(gb->io[0x26] & 0x80)) {
        push_stereo(a, 0.0, 0.0);
        return;
    }

    double ch[4];
    ch[0] = square_sample(gb, 0);
    ch[1] = square_sample(gb, 1);
    ch[2] = wave_sample(gb);
    ch[3] = noise_sample(gb);

    u8 nr50 = gb->io[0x24]; // master volume
    u8 nr51 = gb->io[0x25]; // panning
    double right_vol = (double)((nr50 & 0x07) + 1) / 8.0;
    double left_vol  = (double)(((nr50 >> 4) & 0x07) + 1) / 8.0;

    double left = 0.0;
    double right = 0.0;
    for (int i = 0; i < 4; i++) {
        if (nr51 & (1u << i)) right += ch[i];
        if (nr51 & (1u << (i + 4))) left += ch[i];
    }

    // scale down
    left = (left / 4.0) * left_vol;
    right = (right / 4.0) * right_vol;

    push_stereo(a, left, right);
}

static void clock_length(APU* a) {
    for (int ch = 0; ch < 4; ch++) {
        if (a->channel_enabled[ch] && a->length_enabled[ch] && a->length_counter[ch] > 0) {
            a->length_counter[ch]--;
            if (a->length_counter[ch] == 0) {
                a->channel_enabled[ch] = false;
            }
        }
    }
}

static void clock_envelopes(APU* a) {
    // envelopes apply to channels 1, 2, and 4 channel 3 uses wave output level.
    const int channels[] = {0, 1, 3};
    for (int i = 0; i < 3; i++) {
        int ch = channels[i];
        if (!a->channel_enabled[ch] || !a->env_active[ch]) continue;

        a->env_timer[ch]--;
        if (a->env_timer[ch] <= 0) {
            a->env_timer[ch] = a->env_period[ch] ? a->env_period[ch] : 8;
            int next = (int)a->current_volume[ch] + a->env_dir[ch];
            if (next < 0 || next > 15) {
                a->env_active[ch] = false;
                if (next < 0) a->current_volume[ch] = 0;
            } else {
                a->current_volume[ch] = (u8)next;
            }
        }
    }
}

static void clock_frame_sequencer(APU* a) {

    if (a->frame_seq_step == 0 || a->frame_seq_step == 2 ||
        a->frame_seq_step == 4 || a->frame_seq_step == 6) {
        clock_length(a);
    }

    if (a->frame_seq_step == 7) {
        clock_envelopes(a);
    }

    a->frame_seq_step = (a->frame_seq_step + 1) & 7;
}

void apu_tick(GB* gb, int cycles) {
    APU* a = &gb->apu;

    a->frame_seq_accum += cycles;
    while (a->frame_seq_accum >= FRAME_SEQ_CYCLES) {
        a->frame_seq_accum -= FRAME_SEQ_CYCLES;
        clock_frame_sequencer(a);
    }

    a->sample_accum += cycles * APU_SAMPLE_RATE;
    while (a->sample_accum >= GB_CPU_HZ) {
        a->sample_accum -= GB_CPU_HZ;
        render_sample(gb);
    }
}

int apu_samples_available(const APU* a) {
    return a->sample_frames;
}

const s16* apu_samples_data(const APU* a) {
    return a->samples;
}

void apu_clear_samples(APU* a) {
    a->sample_frames = 0;
}
