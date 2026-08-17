#include "link_diagnostics.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

#define CADENCE_SAMPLE_FRAMES 120

static const char *const category_names[SAMEBOY_LINK_LOG_CATEGORY_COUNT] = {
    [SAMEBOY_LINK_LOG_FRONTEND] = "frontend",
    [SAMEBOY_LINK_LOG_VIDEO] = "video",
    [SAMEBOY_LINK_LOG_AUDIO] = "audio",
    [SAMEBOY_LINK_LOG_TIMING] = "timing",
};

static struct {
    unsigned normal_frames;
    uint64_t first_counter;
} cadence;

void sameboy_link_log(sameboy_link_log_category_t category, const char *format, ...)
{
    if (category < 0 || category >= SAMEBOY_LINK_LOG_CATEGORY_COUNT) {
        category = SAMEBOY_LINK_LOG_FRONTEND;
    }

    fprintf(stderr, "[SameBoy Link][%s] ", category_names[category]);

    va_list arguments;
    va_start(arguments, format);
    vfprintf(stderr, format, arguments);
    va_end(arguments);

    fputc('\n', stderr);
    fflush(stderr);
}

void sameboy_link_diagnostics_start(GB_gameboy_t *gb,
                                    const SDL_PixelFormat *pixel_format,
                                    bool uses_opengl,
                                    const char *audio_driver,
                                    unsigned audio_frequency)
{
    cadence.normal_frames = 0;
    cadence.first_counter = 0;

    sameboy_link_log(SAMEBOY_LINK_LOG_FRONTEND,
                     "session started; renderer=%s",
                     uses_opengl? "OpenGL" : "SDL");

    sameboy_link_log(SAMEBOY_LINK_LOG_VIDEO,
                     "framebuffer=%ux%u pitch=%u format=%s bits_per_pixel=%u bytes_per_pixel=%u",
                     GB_get_screen_width(gb),
                     GB_get_screen_height(gb),
                     GB_get_screen_width(gb) * (unsigned)sizeof(uint32_t),
                     SDL_GetPixelFormatName(pixel_format? pixel_format->format : SDL_PIXELFORMAT_UNKNOWN),
                     pixel_format? pixel_format->BitsPerPixel : 0,
                     pixel_format? pixel_format->BytesPerPixel : 0);

    sameboy_link_log(SAMEBOY_LINK_LOG_AUDIO,
                     "driver=%s frontend_sample_rate=%uHz core_sample_rate=%uHz channels=2 sample_format=S16-native",
                     audio_driver? audio_driver : "unknown",
                     audio_frequency,
                     GB_get_sample_rate(gb));

    sameboy_link_log(SAMEBOY_LINK_LOG_TIMING,
                     "nominal_frame_rate=%.6fHz sample_window=%u_normal_frames",
                     GB_get_usual_frame_rate(gb),
                     CADENCE_SAMPLE_FRAMES);
}

void sameboy_link_diagnostics_vblank(GB_gameboy_t *gb, GB_vblank_type_t type)
{
    if (type != GB_VBLANK_TYPE_NORMAL_FRAME || cadence.normal_frames >= CADENCE_SAMPLE_FRAMES) {
        return;
    }

    uint64_t now = SDL_GetPerformanceCounter();
    if (cadence.normal_frames == 0) {
        cadence.first_counter = now;
    }

    cadence.normal_frames++;
    if (cadence.normal_frames != CADENCE_SAMPLE_FRAMES) {
        return;
    }

    uint64_t frequency = SDL_GetPerformanceFrequency();
    uint64_t elapsed = now - cadence.first_counter;
    if (frequency == 0 || elapsed == 0) {
        sameboy_link_log(SAMEBOY_LINK_LOG_TIMING, "cadence measurement unavailable");
        return;
    }

    double seconds = (double)elapsed / frequency;
    double frames_per_second = (CADENCE_SAMPLE_FRAMES - 1) / seconds;
    sameboy_link_log(SAMEBOY_LINK_LOG_TIMING,
                     "measured_frame_rate=%.3fHz average_frame_time=%.3fms normal_frames=%u nominal_frame_rate=%.6fHz",
                     frames_per_second,
                     seconds * 1000 / (CADENCE_SAMPLE_FRAMES - 1),
                     CADENCE_SAMPLE_FRAMES,
                     GB_get_usual_frame_rate(gb));
}
