#include "local_link.h"

#include "../link_diagnostics.h"

static LocalLink *connected_link;

static signed core_index(LocalLink *link, GB_gameboy_t *gameboy)
{
    EmulatorSlot *slot = game_session_find_slot(link->session, gameboy);
    if (!slot) {
        return -1;
    }
    return (signed)(slot - link->session->slots);
}

static void serial_start(GB_gameboy_t *gameboy, bool bit_to_send)
{
    if (!connected_link) {
        return;
    }

    signed index = core_index(connected_link, gameboy);
    if (index < 0 || index >= GAME_SESSION_SLOT_CAPACITY) {
        return;
    }
    connected_link->bits_to_send[index] = bit_to_send;
}

static bool serial_end(GB_gameboy_t *gameboy)
{
    if (!connected_link) {
        return true;
    }

    signed index = core_index(connected_link, gameboy);
    if (index < 0 || index >= GAME_SESSION_SLOT_CAPACITY) {
        return true;
    }

    unsigned peer_index = !index;
    GB_gameboy_t *peer = &connected_link->session->slots[peer_index].gameboy;
    bool received_bit = GB_serial_get_data_bit(peer);
    GB_serial_set_data_bit(peer, connected_link->bits_to_send[index]);
    connected_link->serial_bit_transfers[index]++;
    return received_bit;
}

static void infrared_output(GB_gameboy_t *gameboy, bool output)
{
    if (!connected_link) {
        return;
    }

    signed index = core_index(connected_link, gameboy);
    if (index < 0 || index >= GAME_SESSION_SLOT_CAPACITY) {
        return;
    }
    GB_set_infrared_input(&connected_link->session->slots[!index].gameboy, output);
}

void local_link_initialize(LocalLink *link, GameSession *session)
{
    *link = (LocalLink){
        .session = session,
        .bits_to_send = {true, true},
    };
}

bool local_link_connect(LocalLink *link)
{
    if (connected_link || link->session->active_slot_count != GAME_SESSION_SLOT_CAPACITY) {
        return false;
    }

    for (unsigned i = 0; i < GAME_SESSION_SLOT_CAPACITY; i++) {
        if (!GB_is_inited(&link->session->slots[i].gameboy)) {
            return false;
        }
    }

    connected_link = link;
    link->connected = true;
    for (unsigned i = 0; i < GAME_SESSION_SLOT_CAPACITY; i++) {
        GB_gameboy_t *gameboy = &link->session->slots[i].gameboy;
        GB_set_serial_transfer_bit_start_callback(gameboy, serial_start);
        GB_set_serial_transfer_bit_end_callback(gameboy, serial_end);
        GB_set_infrared_callback(gameboy, infrared_output);
    }

    sameboy_link_log(SAMEBOY_LINK_LOG_FRONTEND,
                     "local_link connected slots=%u scheduler=cycle_delta",
                     link->session->active_slot_count);
    return true;
}

void local_link_disconnect(LocalLink *link)
{
    if (!link->connected) {
        return;
    }

    for (unsigned i = 0; i < GAME_SESSION_SLOT_CAPACITY; i++) {
        GB_gameboy_t *gameboy = &link->session->slots[i].gameboy;
        if (GB_is_inited(gameboy)) {
            GB_set_serial_transfer_bit_start_callback(gameboy, NULL);
            GB_set_serial_transfer_bit_end_callback(gameboy, NULL);
            GB_set_infrared_callback(gameboy, NULL);
            GB_disconnect_serial(gameboy);
        }
    }

    sameboy_link_log(SAMEBOY_LINK_LOG_FRONTEND,
                     "local_link disconnected serial_bits_p1=%llu serial_bits_p2=%llu",
                     (unsigned long long)link->serial_bit_transfers[0],
                     (unsigned long long)link->serial_bit_transfers[1]);
    link->connected = false;
    if (connected_link == link) {
        connected_link = NULL;
    }
}

bool local_link_run_frame(LocalLink *link)
{
    if (!link->connected) {
        return false;
    }

    EmulatorSlot *first = &link->session->slots[0];
    EmulatorSlot *second = &link->session->slots[1];
    first->vblank_occurred = false;
    second->vblank_occurred = false;

    signed delta = 0;
    while (!first->vblank_occurred || !second->vblank_occurred) {
        if (delta >= 0) {
            delta -= GB_run(&first->gameboy);
        }
        else {
            delta += GB_run(&second->gameboy);
        }
    }

    link->last_cycle_delta = delta;
    link->synchronized_frames++;
    if (!link->scheduler_reported && link->synchronized_frames >= 120) {
        sameboy_link_log(SAMEBOY_LINK_LOG_TIMING,
                         "local_link synchronized_frames=%llu last_cycle_delta=%d serial_bits_p1=%llu serial_bits_p2=%llu audio_samples_p2=%llu",
                         (unsigned long long)link->synchronized_frames,
                         link->last_cycle_delta,
                         (unsigned long long)link->serial_bit_transfers[0],
                         (unsigned long long)link->serial_bit_transfers[1],
                         (unsigned long long)second->audio_sample_count);
        link->scheduler_reported = true;
    }
    return true;
}
