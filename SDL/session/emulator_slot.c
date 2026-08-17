#include "emulator_slot.h"

#include <stdlib.h>
#include <string.h>

void emulator_slot_initialize(EmulatorSlot *slot)
{
    memset(slot, 0, sizeof(*slot));
    slot->active_pixel_buffer = slot->pixel_buffers[0];
    slot->previous_pixel_buffer = slot->pixel_buffers[1];
}

void emulator_slot_deinitialize(EmulatorSlot *slot)
{
    if (GB_is_inited(&slot->gameboy)) {
        GB_free(&slot->gameboy);
    }
    if (slot->rom_path && slot->rom_path_free_function) {
        slot->rom_path_free_function(slot->rom_path);
    }
    free(slot->battery_save_path);
    slot->rom_path = NULL;
    slot->rom_path_free_function = NULL;
    slot->battery_save_path = NULL;
}

void emulator_slot_set_rom_path(EmulatorSlot *slot,
                                const char *rom_path,
                                emulator_slot_free_function_t *free_function)
{
    if (slot->rom_path && slot->rom_path_free_function) {
        slot->rom_path_free_function(slot->rom_path);
    }
    slot->rom_path = (char *)rom_path;
    slot->rom_path_free_function = free_function;
    GB_rewind_reset(&slot->gameboy);
}

bool emulator_slot_set_battery_save_path(EmulatorSlot *slot, const char *battery_save_path)
{
    size_t length = strlen(battery_save_path) + 1;
    char *copy = malloc(length);
    if (!copy) {
        return false;
    }

    memcpy(copy, battery_save_path, length);
    free(slot->battery_save_path);
    slot->battery_save_path = copy;
    return true;
}

void emulator_slot_swap_pixel_buffers(EmulatorSlot *slot)
{
    uint32_t *temporary = slot->active_pixel_buffer;
    slot->active_pixel_buffer = slot->previous_pixel_buffer;
    slot->previous_pixel_buffer = temporary;
}
