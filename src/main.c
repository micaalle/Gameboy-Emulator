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

    bool running = true;
    bool fps_cap = true;
    bool esc_armed = false;
    u32  esc_time_ms = 0;

    const u32 target_ms = 1000 / 60;
    u32 last_tick = SDL_GetTicks();
    u32 fps_last = last_tick;
    int fps_frames = 0;

    while (running) {
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


        u32 now = SDL_GetTicks();
        u32 frame_ms = now - last_tick;
        last_tick = now;

        if (fps_cap && frame_ms < target_ms) {
            SDL_Delay(target_ms - frame_ms);
        }

        fps_frames++;
        if (now - fps_last >= 1000) {
            char t[160];
            snprintf(t, sizeof(t), "%s  |  %d FPS  | cap:%s",
                     win_title, fps_frames, fps_cap ? "on" : "off");
            SDL_SetWindowTitle(window, t);
            fps_frames = 0;
            fps_last = now;
        }
    }

    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    gb_free(&gb);
    return 0;
}
