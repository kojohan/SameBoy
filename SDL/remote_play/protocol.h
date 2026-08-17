#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define REMOTE_PLAY_PROTOCOL_VERSION 4
#define REMOTE_PLAY_INPUT_PACKET_SIZE 36
#define REMOTE_PLAY_CLOCK_SYNC_PING_SIZE 24
#define REMOTE_PLAY_CLOCK_SYNC_PONG_SIZE 40
#define REMOTE_PLAY_VIDEO_HEADER_SIZE 108
#define REMOTE_PLAY_VIDEO_CHUNK_PAYLOAD_SIZE 1024
#define REMOTE_PLAY_VIDEO_PACKET_MAX_SIZE (REMOTE_PLAY_VIDEO_HEADER_SIZE + REMOTE_PLAY_VIDEO_CHUNK_PAYLOAD_SIZE)
#define REMOTE_PLAY_VIDEO_MAX_WIDTH 256
#define REMOTE_PLAY_VIDEO_MAX_HEIGHT 224
#define REMOTE_PLAY_VIDEO_MAX_FRAME_SIZE (REMOTE_PLAY_VIDEO_MAX_WIDTH * REMOTE_PLAY_VIDEO_MAX_HEIGHT * 4)
#define REMOTE_PLAY_VIDEO_MAX_ENCODED_SIZE \
    (REMOTE_PLAY_VIDEO_MAX_FRAME_SIZE + \
     (REMOTE_PLAY_VIDEO_MAX_WIDTH * REMOTE_PLAY_VIDEO_MAX_HEIGHT + 127) / 128)
#define REMOTE_PLAY_VIDEO_MAX_CHUNKS \
    ((REMOTE_PLAY_VIDEO_MAX_ENCODED_SIZE + REMOTE_PLAY_VIDEO_CHUNK_PAYLOAD_SIZE - 1) / \
     REMOTE_PLAY_VIDEO_CHUNK_PAYLOAD_SIZE)
#define REMOTE_PLAY_VIDEO_FORMAT_RGBA8 1
#define REMOTE_PLAY_VIDEO_FORMAT_RLE_RGBA8 2
#define REMOTE_PLAY_AUDIO_HEADER_SIZE 32
#define REMOTE_PLAY_AUDIO_FRAMES_PER_PACKET 240
#define REMOTE_PLAY_AUDIO_BYTES_PER_FRAME 4
#define REMOTE_PLAY_AUDIO_PCM_PAYLOAD_SIZE \
    (REMOTE_PLAY_AUDIO_FRAMES_PER_PACKET * REMOTE_PLAY_AUDIO_BYTES_PER_FRAME)
#define REMOTE_PLAY_AUDIO_PACKET_MAX_PAYLOAD_SIZE REMOTE_PLAY_AUDIO_PCM_PAYLOAD_SIZE
#define REMOTE_PLAY_AUDIO_PACKET_MAX_SIZE \
    (REMOTE_PLAY_AUDIO_HEADER_SIZE + REMOTE_PLAY_AUDIO_PACKET_MAX_PAYLOAD_SIZE)
#define REMOTE_PLAY_AUDIO_CODEC_PCM_S16LE 1
#define REMOTE_PLAY_AUDIO_CODEC_OPUS 2

typedef struct {
    uint32_t session_id;
    uint32_t sequence;
    uint16_t buttons;
    uint64_t client_event_timestamp_us;
    uint64_t client_send_timestamp_us;
} RemotePlayInputPacket;

typedef struct {
    uint32_t session_id;
    uint32_t sequence;
    uint64_t client_send_timestamp_us;
} RemotePlayClockSyncPing;

typedef struct {
    uint32_t session_id;
    uint32_t sequence;
    uint64_t client_send_timestamp_us;
    uint64_t host_receive_timestamp_us;
    uint64_t host_send_timestamp_us;
} RemotePlayClockSyncPong;

typedef struct {
    uint32_t session_id;
    uint64_t frame_sequence;
    uint64_t host_frame_complete_timestamp_us;
    uint16_t width;
    uint16_t height;
    uint16_t chunk_index;
    uint16_t chunk_count;
    uint32_t payload_offset;
    uint32_t frame_size;
    uint16_t payload_size;
    uint8_t pixel_format;
    uint32_t input_sequence;
    uint64_t client_input_event_timestamp_us;
    uint64_t client_input_send_timestamp_us;
    uint64_t host_input_receive_timestamp_us;
    uint64_t host_input_apply_timestamp_us;
    uint64_t host_encode_begin_timestamp_us;
    uint64_t host_encode_end_timestamp_us;
    uint64_t host_send_timestamp_us;
    const uint8_t *payload;
} RemotePlayVideoChunk;

typedef struct {
    uint32_t session_id;
    uint32_t sequence;
    uint64_t host_timestamp_us;
    uint32_t sample_rate;
    uint16_t frame_count;
    uint16_t payload_size;
    uint8_t codec;
    const uint8_t *payload;
} RemotePlayAudioPacket;

void remote_play_encode_input_packet(uint8_t output[REMOTE_PLAY_INPUT_PACKET_SIZE],
                                     const RemotePlayInputPacket *packet);
bool remote_play_decode_input_packet(RemotePlayInputPacket *packet,
                                     const uint8_t *data,
                                     size_t size);
void remote_play_encode_clock_sync_ping(uint8_t output[REMOTE_PLAY_CLOCK_SYNC_PING_SIZE],
                                        const RemotePlayClockSyncPing *packet);
bool remote_play_decode_clock_sync_ping(RemotePlayClockSyncPing *packet,
                                        const uint8_t *data,
                                        size_t size);
void remote_play_encode_clock_sync_pong(uint8_t output[REMOTE_PLAY_CLOCK_SYNC_PONG_SIZE],
                                        const RemotePlayClockSyncPong *packet);
bool remote_play_decode_clock_sync_pong(RemotePlayClockSyncPong *packet,
                                        const uint8_t *data,
                                        size_t size);
bool remote_play_sequence_is_newer(uint32_t candidate, uint32_t current);
size_t remote_play_encode_video_chunk(uint8_t output[REMOTE_PLAY_VIDEO_PACKET_MAX_SIZE],
                                      const RemotePlayVideoChunk *chunk);
bool remote_play_decode_video_chunk(RemotePlayVideoChunk *chunk,
                                    const uint8_t *data,
                                    size_t size);
size_t remote_play_encode_audio_packet(uint8_t output[REMOTE_PLAY_AUDIO_PACKET_MAX_SIZE],
                                       const RemotePlayAudioPacket *packet);
bool remote_play_decode_audio_packet(RemotePlayAudioPacket *packet,
                                     const uint8_t *data,
                                     size_t size);
