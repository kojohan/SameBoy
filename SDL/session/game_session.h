#pragma once

#include <stddef.h>
#include "emulator_slot.h"

#define GAME_SESSION_SLOT_CAPACITY 2

typedef enum {
    GAME_SESSION_SINGLE_PLAYER,
    GAME_SESSION_LOCAL_LINK,
    GAME_SESSION_REMOTE_HOST,
    GAME_SESSION_REMOTE_CLIENT,
    GAME_SESSION_MODE_COUNT,
} GameSessionMode;

typedef struct {
    EmulatorSlot slots[GAME_SESSION_SLOT_CAPACITY];
    GameSessionMode mode;
    unsigned active_slot_count;
    unsigned presentation_slot_count;
    uint32_t composite_framebuffer[GAME_SESSION_SLOT_CAPACITY *
                                   EMULATOR_SLOT_MAX_FRAME_WIDTH *
                                   EMULATOR_SLOT_MAX_FRAME_HEIGHT];
} GameSession;

typedef struct {
    const uint32_t *pixels;
    unsigned width;
    unsigned height;
    unsigned pitch;
    uint64_t sequence;
    uint64_t completed_timestamp_us;
} GameSessionFrame;

void game_session_initialize(GameSession *session);
void game_session_deinitialize(GameSession *session);
const char *game_session_mode_name(GameSessionMode mode);
bool game_session_begin_mode(GameSession *session, GameSessionMode mode);
bool game_session_activate_secondary_slot(GameSession *session);
void game_session_deactivate_secondary_slot(GameSession *session);
void game_session_end_mode(GameSession *session);
EmulatorSlot *game_session_primary_slot(GameSession *session);
GB_gameboy_t *game_session_primary_core(GameSession *session);
EmulatorSlot *game_session_find_slot(GameSession *session, const GB_gameboy_t *gameboy);
bool game_session_get_completed_frame(GameSession *session,
                                      unsigned slot_index,
                                      GameSessionFrame *frame);
bool game_session_set_presentation_slot_count(GameSession *session,
                                              unsigned slot_count);
unsigned game_session_presentation_width(GameSession *session);
unsigned game_session_presentation_height(GameSession *session);
uint32_t *game_session_compose_framebuffers(GameSession *session);
