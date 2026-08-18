#pragma once

#include <stdint.h>

#define REMOTE_PLAY_CLIENT_DISCONNECTED 2

int remote_play_client_run(const char *endpoint, uint32_t session_id);
