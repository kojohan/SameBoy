#include "game_session.h"

#include <string.h>

void game_session_initialize(GameSession *session)
{
    for (unsigned i = 0; i < GAME_SESSION_SLOT_CAPACITY; i++) {
        emulator_slot_initialize(&session->slots[i]);
    }
    session->mode = GAME_SESSION_SINGLE_PLAYER;
    session->active_slot_count = 1;
    session->presentation_slot_count = 1;
}

void game_session_deinitialize(GameSession *session)
{
    for (unsigned i = 0; i < GAME_SESSION_SLOT_CAPACITY; i++) {
        emulator_slot_deinitialize(&session->slots[i]);
    }
    session->active_slot_count = 0;
    session->presentation_slot_count = 0;
    session->mode = GAME_SESSION_SINGLE_PLAYER;
}

const char *game_session_mode_name(GameSessionMode mode)
{
    static const char *names[] = {
        [GAME_SESSION_SINGLE_PLAYER] = "single_player",
        [GAME_SESSION_LOCAL_LINK] = "local_link",
        [GAME_SESSION_REMOTE_HOST] = "remote_host",
        [GAME_SESSION_REMOTE_CLIENT] = "remote_client",
    };
    if ((unsigned)mode >= GAME_SESSION_MODE_COUNT) {
        return "invalid";
    }
    return names[mode];
}

bool game_session_begin_mode(GameSession *session, GameSessionMode mode)
{
    if (!session || (unsigned)mode >= GAME_SESSION_MODE_COUNT ||
        session->mode != GAME_SESSION_SINGLE_PLAYER ||
        session->active_slot_count != 1 ||
        session->presentation_slot_count != 1) {
        return false;
    }

    if (mode == GAME_SESSION_SINGLE_PLAYER) {
        return true;
    }

    session->mode = mode;
    if (mode == GAME_SESSION_REMOTE_CLIENT) {
        session->active_slot_count = 0;
        session->presentation_slot_count = 0;
    }
    return true;
}

bool game_session_activate_secondary_slot(GameSession *session)
{
    if (!session ||
        (session->mode != GAME_SESSION_LOCAL_LINK &&
         session->mode != GAME_SESSION_REMOTE_HOST) ||
        session->active_slot_count != 1 ||
        !GB_is_inited(&session->slots[1].gameboy)) {
        return false;
    }

    session->active_slot_count = 2;
    session->presentation_slot_count = 2;
    return true;
}

void game_session_deactivate_secondary_slot(GameSession *session)
{
    if (!session) {
        return;
    }

    emulator_slot_deinitialize(&session->slots[1]);
    emulator_slot_initialize(&session->slots[1]);
    session->active_slot_count = 1;
    session->presentation_slot_count = 1;
}

void game_session_end_mode(GameSession *session)
{
    if (!session) {
        return;
    }

    game_session_deactivate_secondary_slot(session);
    session->mode = GAME_SESSION_SINGLE_PLAYER;
    session->active_slot_count = 1;
    session->presentation_slot_count = 1;
}

EmulatorSlot *game_session_primary_slot(GameSession *session)
{
    return &session->slots[0];
}

GB_gameboy_t *game_session_primary_core(GameSession *session)
{
    return &game_session_primary_slot(session)->gameboy;
}

EmulatorSlot *game_session_find_slot(GameSession *session, const GB_gameboy_t *gameboy)
{
    for (unsigned i = 0; i < GAME_SESSION_SLOT_CAPACITY; i++) {
        if (&session->slots[i].gameboy == gameboy) {
            return &session->slots[i];
        }
    }
    return NULL;
}

bool game_session_get_completed_frame(GameSession *session,
                                      unsigned slot_index,
                                      GameSessionFrame *frame)
{
    if (!frame || slot_index >= session->active_slot_count) {
        return false;
    }

    EmulatorSlot *slot = &session->slots[slot_index];
    if (!GB_is_inited(&slot->gameboy) || slot->completed_frame_sequence == 0) {
        return false;
    }

    unsigned width = GB_get_screen_width(&slot->gameboy);
    *frame = (GameSessionFrame){
        .pixels = slot->previous_pixel_buffer,
        .width = width,
        .height = GB_get_screen_height(&slot->gameboy),
        .pitch = width * sizeof(uint32_t),
        .sequence = slot->completed_frame_sequence,
        .completed_timestamp_us = slot->completed_frame_timestamp_us,
    };
    return true;
}

bool game_session_set_presentation_slot_count(GameSession *session,
                                              unsigned slot_count)
{
    if (!slot_count || slot_count > session->active_slot_count) {
        return false;
    }
    session->presentation_slot_count = slot_count;
    return true;
}

unsigned game_session_presentation_width(GameSession *session)
{
    if (!session->presentation_slot_count) {
        return 160;
    }
    unsigned width = 0;
    for (unsigned i = 0; i < session->presentation_slot_count; i++) {
        width += GB_get_screen_width(&session->slots[i].gameboy);
    }
    return width;
}

unsigned game_session_presentation_height(GameSession *session)
{
    if (!session->presentation_slot_count) {
        return 144;
    }
    unsigned height = 0;
    for (unsigned i = 0; i < session->presentation_slot_count; i++) {
        unsigned slot_height = GB_get_screen_height(&session->slots[i].gameboy);
        if (slot_height > height) {
            height = slot_height;
        }
    }
    return height;
}

uint32_t *game_session_compose_framebuffers(GameSession *session)
{
    if (session->presentation_slot_count == 1) {
        EmulatorSlot *slot = &session->slots[0];
        if ((session->mode == GAME_SESSION_LOCAL_LINK ||
             session->mode == GAME_SESSION_REMOTE_HOST) &&
            slot->completed_frame_sequence) {
            return slot->previous_pixel_buffer;
        }
        return session->slots[0].active_pixel_buffer;
    }

    unsigned presentation_width = game_session_presentation_width(session);
    unsigned presentation_height = game_session_presentation_height(session);
    memset(session->composite_framebuffer,
           0,
           presentation_width * presentation_height * sizeof(uint32_t));

    unsigned x_offset = 0;
    for (unsigned i = 0; i < session->presentation_slot_count; i++) {
        EmulatorSlot *slot = &session->slots[i];
        unsigned width = GB_get_screen_width(&slot->gameboy);
        unsigned height = GB_get_screen_height(&slot->gameboy);
        for (unsigned y = 0; y < height; y++) {
            memcpy(&session->composite_framebuffer[y * presentation_width + x_offset],
                   &slot->previous_pixel_buffer[y * width],
                   width * sizeof(uint32_t));
        }
        x_offset += width;
    }
    return session->composite_framebuffer;
}
