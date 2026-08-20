#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define REMOTE_PLAY_AUTH_KEY_SIZE 16
#define REMOTE_PLAY_AUTH_KEY_HEX_SIZE (REMOTE_PLAY_AUTH_KEY_SIZE * 2)
#define REMOTE_PLAY_AUTH_TAG_SIZE 16
#define REMOTE_PLAY_AUTH_NONCE_SIZE 16

bool remote_play_auth_parse_key(
    uint8_t key[REMOTE_PLAY_AUTH_KEY_SIZE],
    const char *hex);
void remote_play_auth_format_key_hex(
    char output[REMOTE_PLAY_AUTH_KEY_HEX_SIZE + 1],
    const uint8_t key[REMOTE_PLAY_AUTH_KEY_SIZE]);
bool remote_play_auth_random(uint8_t *output, size_t size);
bool remote_play_auth_hmac(
    uint8_t tag[REMOTE_PLAY_AUTH_TAG_SIZE],
    const uint8_t key[REMOTE_PLAY_AUTH_KEY_SIZE],
    const uint8_t *data,
    size_t size);
bool remote_play_auth_tags_equal(
    const uint8_t left[REMOTE_PLAY_AUTH_TAG_SIZE],
    const uint8_t right[REMOTE_PLAY_AUTH_TAG_SIZE]);
