#include "gb.h"
#include "util.h"

#if defined(__has_include)
  #if __has_include(<SDL2/SDL.h>)
    #include <SDL2/SDL.h>
  #else
    #include <SDL.h>
  #endif
#else
  #include <SDL2/SDL.h>
#endif
#include <stdio.h>
#include <string.h>

#ifndef GBEMU_DEFAULT_SCALE
#define GBEMU_DEFAULT_SCALE 6
#endif


static double perf_now_ms(void) {
    return (double)SDL_GetPerformanceCounter() * 1000.0 /
           (double)SDL_GetPerformanceFrequency();
}

static void set_button(GB* gb, bool is_dpad, int bit, bool pressed) {

    u8* group = is_dpad ? &gb->joyp_dpad : &gb->joyp_buttons;
    u8 before = *group;
    if (pressed) *group = (u8)(*group & ~(1u << bit));
    else         *group = (u8)(*group |  (1u << bit));
    if ((before & (1u << bit)) && !(*group & (1u << bit))) {
        gb_request_interrupt(gb, INT_JOYPAD);
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s path/to/rom.gb\n", argv[0]);
        return 1;
    }

    GB gb;
    if (!gb_init(&gb, argv[1])) {
        fprintf(stderr, "Failed to load ROM: %s\n", argv[1]);
        return 1;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS | SDL_INIT_TIMER) != 0) {
        die("SDL_Init failed: %s", SDL_GetError());
    }

    char win_title[128];
    snprintf(win_title, sizeof(win_title), "gb emulator — %s", gb.cart.title[0] ? gb.cart.title : "ROM");

    int scale = GBEMU_DEFAULT_SCALE;
    SDL_Window* window = SDL_CreateWindow(
        win_title,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        160 * scale, 144 * scale,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    if (!window) die("SDL_CreateWindow failed: %s", SDL_GetError());

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) die("SDL_CreateRenderer failed: %s", SDL_GetError());

    SDL_Texture* tex = SDL_CreateTexture(renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        160, 144
    );
    if (!tex) die("SDL_CreateTexture failed: %s", SDL_GetError());

    SDL_AudioSpec want;
    SDL_zero(want);
    want.freq = APU_SAMPLE_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = NULL; // queue-based audio

    SDL_AudioDeviceID audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
    if (audio_dev == 0) {
        fprintf(stderr, "Audio disabled: SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
    } else {
        SDL_PauseAudioDevice(audio_dev, 0);
    }

    bool running = true;
    bool fps_cap = true;
    bool esc_armed = false;
    u32  esc_time_ms = 0;

    // real DMG frame timing: 70224 CPU cycles per frame at 4194304 Hz.
    // this is about 59.7275 FPS 
    const double target_frame_ms = 1000.0 * 70224.0 / 4194304.0;
    double fps_last = perf_now_ms();
    int fps_frames = 0;

    while (running) {
        double frame_start_ms = perf_now_ms();
        u32 loop_now = SDL_GetTicks();
        if (esc_armed && (loop_now - esc_time_ms) > 1500) {
            esc_armed = false;
            SDL_SetWindowTitle(window, win_title);
        }

        // events
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;

            if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
                bool down = (e.type == SDL_KEYDOWN);
                SDL_Keycode k = e.key.keysym.sym;

                // no fat fingering esc takes 2 click fast
                if (down && k == SDLK_ESCAPE) {
                    u32 t = SDL_GetTicks();
                    if (esc_armed && (t - esc_time_ms) <= 1500) {
                        running = false;
                    } else {
                        esc_armed = true;
                        esc_time_ms = t;
                        char tbuf[192];
                        snprintf(tbuf, sizeof(tbuf), "%s  |  Press ESC again to quit", win_title);
                        SDL_SetWindowTitle(window, tbuf);
                    }
                }

                if (down && k == SDLK_f) fps_cap = !fps_cap;
                if (down && k == SDLK_r) gb_reset(&gb);

                // test!! window scaling
                if (down && (e.key.keysym.mod & KMOD_CTRL)) {
                    if (k == SDLK_EQUALS || k == SDLK_PLUS || k == SDLK_KP_PLUS) {
                        if (scale < 12) scale++;
                        SDL_SetWindowSize(window, 160 * scale, 144 * scale);
                    }
                    if (k == SDLK_MINUS || k == SDLK_KP_MINUS) {
                        if (scale > 1) scale--;
                        SDL_SetWindowSize(window, 160 * scale, 144 * scale);
                    }
                }

                if (k == SDLK_RIGHT || k == SDLK_d) set_button(&gb, true, 0, down);
                if (k == SDLK_LEFT  || k == SDLK_a) set_button(&gb, true, 1, down);
                if (k == SDLK_UP    || k == SDLK_w) set_button(&gb, true, 2, down);
                if (k == SDLK_DOWN  || k == SDLK_s) set_button(&gb, true, 3, down);


                if (k == SDLK_RETURN || k == SDLK_SPACE || k == SDLK_z)
                    set_button(&gb, false, 0, down); 

                if (k == SDLK_x || k == SDLK_LSHIFT)
                    set_button(&gb, false, 1, down); 

                if (k == SDLK_BACKSPACE)
                    set_button(&gb, false, 2, down);

                if (k == SDLK_TAB)
                    set_button(&gb, false, 3, down);
            }
        }


        gb_run_frame(&gb);

        if (audio_dev != 0) {
            int frames = apu_samples_available(&gb.apu);
            if (frames > 0) {
                Uint32 queued = SDL_GetQueuedAudioSize(audio_dev);
                if (queued < (Uint32)(APU_SAMPLE_RATE / 5 * 2 * sizeof(s16))) {
                    SDL_QueueAudio(audio_dev, apu_samples_data(&gb.apu),
                                   (Uint32)(frames * 2 * sizeof(s16)));
                }
                apu_clear_samples(&gb.apu);
            }
        }

        void* pixels = NULL;
        int pitch = 0;
        if (SDL_LockTexture(tex, NULL, &pixels, &pitch) != 0) {
            die("SDL_LockTexture failed: %s", SDL_GetError());
        }

        for (int y = 0; y < 144; y++) {
            memcpy((u8*)pixels + y * pitch, &gb.ppu.framebuffer[y * 160], 160 * sizeof(u32));
        }
        SDL_UnlockTexture(tex);

        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, tex, NULL, NULL);
        SDL_RenderPresent(renderer);


        if (fps_cap) {
            double elapsed_ms = perf_now_ms() - frame_start_ms;
            if (elapsed_ms < target_frame_ms) {
                SDL_Delay((Uint32)(target_frame_ms - elapsed_ms));
            }

            // delay for sound creep
            while ((perf_now_ms() - frame_start_ms) < target_frame_ms) {
                SDL_Delay(0);
            }
        }

        double now = perf_now_ms();
        fps_frames++;
        if (now - fps_last >= 1000.0) {
            char t[160];
            snprintf(t, sizeof(t), "%s  |  %d FPS  | cap:%s",
                     win_title, fps_frames, fps_cap ? "on" : "off");
            SDL_SetWindowTitle(window, t);
            fps_frames = 0;
            fps_last = now;
        }
    }

    if (audio_dev != 0) SDL_CloseAudioDevice(audio_dev);

    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    gb_free(&gb);
    return 0;
}

