#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <Core/gb.h>

#define EMULATOR_SLOT_MAX_FRAME_WIDTH 256
#define EMULATOR_SLOT_MAX_FRAME_HEIGHT 224

typedef void emulator_slot_free_function_t(void *pointer);

typedef struct {
    GB_gameboy_t gameboy;
    uint32_t pixel_buffers[2][EMULATOR_SLOT_MAX_FRAME_WIDTH * EMULATOR_SLOT_MAX_FRAME_HEIGHT];
    uint32_t *active_pixel_buffer;
    uint32_t *previous_pixel_buffer;
    char *rom_path;
    emulator_slot_free_function_t *rom_path_free_function;
    char *battery_save_path;
    bool battery_dirty;
    unsigned battery_timer;
    bool vblank_occurred;
    uint64_t completed_frame_sequence;
    uint64_t completed_frame_timestamp_us;
    uint64_t audio_sample_count;
    bool key_state[GB_KEY_MAX];
} EmulatorSlot;

void emulator_slot_initialize(EmulatorSlot *slot);
void emulator_slot_deinitialize(EmulatorSlot *slot);
void emulator_slot_set_rom_path(EmulatorSlot *slot,
                                const char *rom_path,
                                emulator_slot_free_function_t *free_function);
bool emulator_slot_set_battery_save_path(EmulatorSlot *slot, const char *battery_save_path);
void emulator_slot_swap_pixel_buffers(EmulatorSlot *slot);
