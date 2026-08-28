#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "game_session.h"

typedef struct {
    GameSession *session;
    bool connected;
    bool bits_to_send[GAME_SESSION_SLOT_CAPACITY];
    uint64_t serial_bit_transfers[GAME_SESSION_SLOT_CAPACITY];
    uint64_t synchronized_frames;
    signed last_cycle_delta;
    bool scheduler_reported;
} LocalLink;

void local_link_initialize(LocalLink *link, GameSession *session);
bool local_link_connect(LocalLink *link);
void local_link_disconnect(LocalLink *link);
bool local_link_run_frame(LocalLink *link);
