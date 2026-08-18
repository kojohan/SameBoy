#pragma once

#include <stdbool.h>
#include <SDL.h>
#include "game_session.h"

enum {
    MULTIPLAYER_BUTTON_RIGHT  = 1u << 0,
    MULTIPLAYER_BUTTON_LEFT   = 1u << 1,
    MULTIPLAYER_BUTTON_UP     = 1u << 2,
    MULTIPLAYER_BUTTON_DOWN   = 1u << 3,
    MULTIPLAYER_BUTTON_A      = 1u << 4,
    MULTIPLAYER_BUTTON_B      = 1u << 5,
    MULTIPLAYER_BUTTON_SELECT = 1u << 6,
    MULTIPLAYER_BUTTON_START  = 1u << 7,
    MULTIPLAYER_BUTTON_ALL    = 0xFF,
};

bool multiplayer_input_button_for_scancode(SDL_Scancode scancode, uint16_t *button);
bool multiplayer_input_button_for_remote_scancode(SDL_Scancode scancode, uint16_t *button);
uint16_t multiplayer_input_current_button_mask(const GameSession *session);
void multiplayer_input_apply_button_mask(GameSession *session, uint16_t buttons);
void multiplayer_input_set_button_state(GameSession *session,
                                         uint16_t button,
                                         bool pressed);
bool multiplayer_input_handle_keyboard_event(GameSession *session, const SDL_KeyboardEvent *event);
