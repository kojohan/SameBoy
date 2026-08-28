#pragma once

#include <stdbool.h>
#include <SDL.h>
#include <Core/gb.h>

typedef enum {
    SAMEBOY_LINK_LOG_FRONTEND,
    SAMEBOY_LINK_LOG_VIDEO,
    SAMEBOY_LINK_LOG_AUDIO,
    SAMEBOY_LINK_LOG_TIMING,
    SAMEBOY_LINK_LOG_CATEGORY_COUNT,
} sameboy_link_log_category_t;

void sameboy_link_log(sameboy_link_log_category_t category, const char *format, ...);
void sameboy_link_diagnostics_start(GB_gameboy_t *gb,
                                    const SDL_PixelFormat *pixel_format,
                                    bool uses_opengl,
                                    const char *audio_driver,
                                    unsigned audio_frequency);
void sameboy_link_diagnostics_vblank(GB_gameboy_t *gb, GB_vblank_type_t type);
