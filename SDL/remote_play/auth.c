#include "auth.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#endif

static int hex_value(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    ch = (char)tolower((unsigned char)ch);
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    return -1;
}

bool remote_play_auth_parse_key(
    uint8_t key[REMOTE_PLAY_AUTH_KEY_SIZE],
    const char *hex)
{
    if (!key || !hex || strlen(hex) != REMOTE_PLAY_AUTH_KEY_HEX_SIZE) {
        return false;
    }
    uint8_t parsed[REMOTE_PLAY_AUTH_KEY_SIZE];
    for (size_t i = 0; i < REMOTE_PLAY_AUTH_KEY_SIZE; i++) {
        int high = hex_value(hex[i * 2]);
        int low = hex_value(hex[i * 2 + 1]);
        if (high < 0 || low < 0) {
            memset(parsed, 0, sizeof(parsed));
            return false;
        }
        parsed[i] = (uint8_t)(high << 4 | low);
    }
    memcpy(key, parsed, sizeof(parsed));
    memset(parsed, 0, sizeof(parsed));
    return true;
}

bool remote_play_auth_random(uint8_t *output, size_t size)
{
    if (!output || !size || size > UINT32_MAX) return false;
#ifdef _WIN32
    return BCryptGenRandom(NULL,
                           output,
                           (ULONG)size,
                           BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0;
#else
    return false;
#endif
}

void remote_play_auth_format_key_hex(
    char output[REMOTE_PLAY_AUTH_KEY_HEX_SIZE + 1],
    const uint8_t key[REMOTE_PLAY_AUTH_KEY_SIZE])
{
    static const char digits[] = "0123456789abcdef";
    if (!output || !key) return;
    for (size_t i = 0; i < REMOTE_PLAY_AUTH_KEY_SIZE; i++) {
        output[i * 2] = digits[key[i] >> 4];
        output[i * 2 + 1] = digits[key[i] & 0x0F];
    }
    output[REMOTE_PLAY_AUTH_KEY_HEX_SIZE] = 0;
}

bool remote_play_auth_hmac(
    uint8_t tag[REMOTE_PLAY_AUTH_TAG_SIZE],
    const uint8_t key[REMOTE_PLAY_AUTH_KEY_SIZE],
    const uint8_t *data,
    size_t size)
{
    if (!tag || !key || (!data && size) || size > UINT32_MAX) return false;
#ifdef _WIN32
    BCRYPT_ALG_HANDLE algorithm = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    uint8_t digest[32];
    uint8_t *object = NULL;
    DWORD object_size = 0;
    DWORD result_size = 0;
    bool success = false;

    if (BCryptOpenAlgorithmProvider(&algorithm,
                                    BCRYPT_SHA256_ALGORITHM,
                                    NULL,
                                    BCRYPT_ALG_HANDLE_HMAC_FLAG) < 0 ||
        BCryptGetProperty(algorithm,
                          BCRYPT_OBJECT_LENGTH,
                          (PUCHAR)&object_size,
                          sizeof(object_size),
                          &result_size,
                          0) < 0 ||
        result_size != sizeof(object_size)) {
        goto cleanup;
    }
    object = malloc(object_size);
    if (!object ||
        BCryptCreateHash(algorithm,
                         &hash,
                         object,
                         object_size,
                         (PUCHAR)key,
                         REMOTE_PLAY_AUTH_KEY_SIZE,
                         0) < 0 ||
        BCryptHashData(hash, (PUCHAR)data, (ULONG)size, 0) < 0 ||
        BCryptFinishHash(hash, digest, sizeof(digest), 0) < 0) {
        goto cleanup;
    }
    memcpy(tag, digest, REMOTE_PLAY_AUTH_TAG_SIZE);
    success = true;

cleanup:
    memset(digest, 0, sizeof(digest));
    if (hash) BCryptDestroyHash(hash);
    free(object);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    return success;
#else
    return false;
#endif
}

bool remote_play_auth_tags_equal(
    const uint8_t left[REMOTE_PLAY_AUTH_TAG_SIZE],
    const uint8_t right[REMOTE_PLAY_AUTH_TAG_SIZE])
{
    if (!left || !right) return false;
    uint8_t difference = 0;
    for (size_t i = 0; i < REMOTE_PLAY_AUTH_TAG_SIZE; i++) {
        difference |= left[i] ^ right[i];
    }
    return difference == 0;
}
