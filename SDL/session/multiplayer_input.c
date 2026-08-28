#include "multiplayer_input.h"
#include "../configuration.h"

static const struct {
    uint16_t button;
    GB_key_t key;
} button_keys[] = {
    {MULTIPLAYER_BUTTON_RIGHT, GB_KEY_RIGHT},
    {MULTIPLAYER_BUTTON_LEFT, GB_KEY_LEFT},
    {MULTIPLAYER_BUTTON_UP, GB_KEY_UP},
    {MULTIPLAYER_BUTTON_DOWN, GB_KEY_DOWN},
    {MULTIPLAYER_BUTTON_A, GB_KEY_A},
    {MULTIPLAYER_BUTTON_B, GB_KEY_B},
    {MULTIPLAYER_BUTTON_SELECT, GB_KEY_SELECT},
    {MULTIPLAYER_BUTTON_START, GB_KEY_START},
};

bool multiplayer_input_button_for_scancode(SDL_Scancode scancode, uint16_t *button)
{
    if (!button) {
        return false;
    }
    for (unsigned i = 0; i < sizeof(button_keys) / sizeof(button_keys[0]); i++) {
        if (configuration.p2_keys[button_keys[i].key] == scancode) {
            *button = button_keys[i].button;
            return true;
        }
    }
    return false;
}

bool multiplayer_input_button_for_remote_scancode(SDL_Scancode scancode,
                                                   uint16_t *button)
{
    return multiplayer_input_button_for_scancode(scancode, button);
}

uint16_t multiplayer_input_current_button_mask(const GameSession *session)
{
    if (session->active_slot_count < 2) {
        return 0;
    }

    const EmulatorSlot *slot = &session->slots[1];
    uint16_t buttons = 0;
    for (unsigned i = 0; i < sizeof(button_keys) / sizeof(button_keys[0]); i++) {
        if (slot->key_state[button_keys[i].key]) {
            buttons |= button_keys[i].button;
        }
    }
    return buttons;
}

void multiplayer_input_apply_button_mask(GameSession *session, uint16_t buttons)
{
    if (session->active_slot_count < 2) {
        return;
    }

    EmulatorSlot *slot = &session->slots[1];
    for (unsigned i = 0; i < sizeof(button_keys) / sizeof(button_keys[0]); i++) {
        bool pressed = (buttons & button_keys[i].button) != 0;
        slot->key_state[button_keys[i].key] = pressed;
        GB_set_key_state(&slot->gameboy, button_keys[i].key, pressed);
    }
}

void multiplayer_input_set_button_state(GameSession *session,
                                         uint16_t button,
                                         bool pressed)
{
    uint16_t buttons = multiplayer_input_current_button_mask(session);
    if (pressed) {
        buttons |= button;
    }
    else {
        buttons &= ~button;
    }
    multiplayer_input_apply_button_mask(session, buttons);
}

bool multiplayer_input_handle_keyboard_event(GameSession *session, const SDL_KeyboardEvent *event)
{
    if (session->active_slot_count < 2) {
        return false;
    }

    uint16_t button;
    if (!multiplayer_input_button_for_scancode(event->keysym.scancode, &button)) {
        return false;
    }

    bool pressed = event->type == SDL_KEYDOWN;
    multiplayer_input_set_button_state(session, button, pressed);
    return true;
}
