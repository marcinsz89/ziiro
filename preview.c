#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <lvgl.h>

#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "studio.h"

enum { PIXEL_BYTES = sizeof(uint16_t), PITCH = STUDIO_WIDTH * PIXEL_BYTES };

typedef struct {
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    bool failed;
} PreviewDisplay;

static alignas(16) uint8_t pixels[PITCH * STUDIO_HEIGHT];

static uint32_t time_ms(void)
{
    return SDL_GetTicks();
}

static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *data)
{
    PreviewDisplay *preview = lv_display_get_user_data(display);
    (void)area; /* Full render mode delivers a complete 320 × 170 frame. */
    if (SDL_UpdateTexture(preview->texture, NULL, data, PITCH) < 0 ||
        SDL_RenderClear(preview->renderer) < 0 ||
        SDL_RenderCopy(preview->renderer, preview->texture, NULL, NULL) < 0) {
        fprintf(stderr, "Cannot draw preview: %s\n", SDL_GetError());
        preview->failed = true;
    } else {
        SDL_RenderPresent(preview->renderer);
    }
    lv_display_flush_ready(display);
}

static bool handle_key(SDL_Keycode key, int repeat)
{
    if (studio_search_editing()) {
        switch (key) {
        case SDLK_RETURN:
        case SDLK_KP_ENTER: return repeat || studio_input(STUDIO_SELECT);
        case SDLK_ESCAPE: return repeat || studio_input(STUDIO_BACK);
        case SDLK_BACKSPACE: studio_search_edit('\b'); return true;
        default:
            if (!repeat && key >= ' ' && key <= '~') studio_search_edit((char)key);
            return true;
        }
    }
    switch (key) {
    case SDLK_UP:
    case SDLK_f:
        return studio_input(STUDIO_UP);
    case SDLK_DOWN:
    case SDLK_x:
        return studio_input(STUDIO_DOWN);
    default:
        break;
    }

    if (repeat) {
        return true;
    }
    switch (key) {
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        return studio_input(STUDIO_SELECT);
    case SDLK_ESCAPE:
        return studio_input(STUDIO_BACK);
    case SDLK_b:
        return studio_input(STUDIO_BACKUP);
    case SDLK_r:
        studio_retry_nfc();
        return true;
    case SDLK_u:
        return studio_input(STUDIO_SETTINGS);
    case SDLK_TAB:
        return studio_input(STUDIO_VIEW);
    case SDLK_g:
        return studio_input(STUDIO_GROUP);
    case SDLK_t:
        return studio_input(STUDIO_REFRESH);
    case SDLK_s:
        return studio_input(STUDIO_SEARCH);
    case SDLK_i:
        return studio_input(STUDIO_GAMES);
    default:
        return true;
    }
}

static int save_screenshot(const char *path)
{
    SDL_Surface *image = SDL_CreateRGBSurfaceWithFormatFrom(
        pixels, STUDIO_WIDTH, STUDIO_HEIGHT, 16, PITCH, SDL_PIXELFORMAT_RGB565);
    int status = image ? SDL_SaveBMP(image, path) : -1;
    if (status < 0) {
        fprintf(stderr, "Cannot save screenshot: %s\n", SDL_GetError());
    }
    SDL_FreeSurface(image);
    return status < 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--convert-artwork") == 0) return studio_convert_artwork();
    const char *screenshot = NULL;
    const char *backups = NULL;
    const char *artwork = NULL;
    const char *profile = NULL;
    for (int i = 1; i < argc; i += 2) {
        if (i + 1 < argc) {
            if (strcmp(argv[i], "--screenshot") == 0) {
                screenshot = argv[i + 1];
                continue;
            }
            if (strcmp(argv[i], "--library") == 0) {
                backups = argv[i + 1];
                continue;
            }
            if (strcmp(argv[i], "--artwork") == 0) {
                artwork = argv[i + 1];
                continue;
            }
            if (strcmp(argv[i], "--profile") == 0) {
                profile = argv[i + 1];
                continue;
            }
        }
        fprintf(stderr, "Usage: %s [--profile DIR] [--library DIR] [--artwork DIR] [--screenshot PATH.bmp]\n", argv[0]);
        return 2;
    }

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "Cannot initialize SDL: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window *window = SDL_CreateWindow(
        "Ziiro | Cardputer Zero preview", SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED, STUDIO_WIDTH * 3, STUDIO_HEIGHT * 3,
        screenshot ? SDL_WINDOW_HIDDEN : SDL_WINDOW_RESIZABLE);
    SDL_Renderer *renderer = window ? SDL_CreateRenderer(window, -1, 0) : NULL;
    SDL_Texture *texture = renderer ? SDL_CreateTexture(
        renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING,
        STUDIO_WIDTH, STUDIO_HEIGHT) : NULL;
    char *base = SDL_GetBasePath();
    char *default_profile = NULL;
    if (base && !profile) {
        SDL_asprintf(&default_profile, "%sprofile", base);
        profile = default_profile;
    }
    int result = 1;

    if (texture && base && profile &&
        SDL_RenderSetLogicalSize(renderer, STUDIO_WIDTH, STUDIO_HEIGHT) == 0) {
        PreviewDisplay preview = { .renderer = renderer, .texture = texture };
        lv_init();
        lv_tick_set_cb(time_ms);
        lv_display_t *display = lv_display_create(STUDIO_WIDTH, STUDIO_HEIGHT);
        lv_display_set_user_data(display, &preview);
        lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
        lv_display_set_buffers(display, pixels, NULL, sizeof(pixels), LV_DISPLAY_RENDER_MODE_FULL);
        lv_display_set_flush_cb(display, flush);
        if (!studio_init(profile, base, backups, artwork)) {
            fprintf(stderr, "Cannot open the Ziiro profile at %s.\n", profile);
            preview.failed = true;
        } else {
            lv_refr_now(display);
        }
        if (screenshot && !preview.failed) {
            result = save_screenshot(screenshot);
        } else {
            bool running = true;
            while (running && !preview.failed) {
                SDL_Event event;
                while (running && SDL_PollEvent(&event)) {
                    if (event.type == SDL_QUIT) {
                        running = false;
                    } else if (event.type == SDL_KEYDOWN) {
                        running = handle_key(event.key.keysym.sym, event.key.repeat);
                    }
                }
                lv_timer_handler();
                SDL_Delay(5);
            }
            result = preview.failed;
        }
        studio_shutdown();
        lv_deinit();
    } else {
        fprintf(stderr, "Cannot create preview window: %s\n", SDL_GetError());
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_free(default_profile);
    SDL_free(base);
    SDL_Quit();
    return result;
}
