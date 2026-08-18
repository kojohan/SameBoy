#pragma once

#include <SDL.h>
#include "configuration.h"

#define JOYSTICK_HIGH 0x4000
#define JOYSTICK_LOW 0x3800

void connect_joypad(void);
int joypad_player_for_instance(SDL_JoystickID instance_id);
joypad_button_t get_player_joypad_button(unsigned player, uint8_t physical_button);
joypad_axis_t get_player_joypad_axis(unsigned player, uint8_t physical_axis);
