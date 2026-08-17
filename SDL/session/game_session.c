#include "game_session.h"

#include <string.h>

void game_session_initialize(GameSession *session)
{
    for (unsigned i = 0; i < GAME_SESSION_SLOT_CAPACITY; i++) {
        emulator_slot_initialize(&session->slots[i]);
    }
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
    unsigned width = 0;
    for (unsigned i = 0; i < session->presentation_slot_count; i++) {
        width += GB_get_screen_width(&session->slots[i].gameboy);
    }
    return width;
}

unsigned game_session_presentation_height(GameSession *session)
{
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
