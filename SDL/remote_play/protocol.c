#include "protocol.h"

#include <string.h>

#define INPUT_PACKET_TYPE 1
#define VIDEO_PACKET_TYPE 2
#define AUDIO_PACKET_TYPE 3
#define CLOCK_SYNC_PING_PACKET_TYPE 4
#define CLOCK_SYNC_PONG_PACKET_TYPE 5
#define VALID_BUTTON_MASK 0xFF
#define AUDIO_CHANNELS_STEREO 2

static void write_u16(uint8_t *output, uint16_t value)
{
    output[0] = value >> 8;
    output[1] = value;
}

static void write_u32(uint8_t *output, uint32_t value)
{
    output[0] = value >> 24;
    output[1] = value >> 16;
    output[2] = value >> 8;
    output[3] = value;
}

static void write_u64(uint8_t *output, uint64_t value)
{
    write_u32(output, value >> 32);
    write_u32(output + 4, value);
}

static uint16_t read_u16(const uint8_t *input)
{
    return (uint16_t)input[0] << 8 | input[1];
}

static uint32_t read_u32(const uint8_t *input)
{
    return (uint32_t)input[0] << 24 |
           (uint32_t)input[1] << 16 |
           (uint32_t)input[2] << 8 |
           input[3];
}

static uint64_t read_u64(const uint8_t *input)
{
    return (uint64_t)read_u32(input) << 32 | read_u32(input + 4);
}

void remote_play_encode_input_packet(uint8_t output[REMOTE_PLAY_INPUT_PACKET_SIZE],
                                     const RemotePlayInputPacket *packet)
{
    output[0] = 'S';
    output[1] = 'B';
    output[2] = 'L';
    output[3] = 'K';
    write_u16(output + 4, REMOTE_PLAY_PROTOCOL_VERSION);
    output[6] = INPUT_PACKET_TYPE;
    output[7] = 0;
    write_u32(output + 8, packet->session_id);
    write_u32(output + 12, packet->sequence);
    write_u16(output + 16, packet->buttons);
    write_u16(output + 18, 0);
    write_u64(output + 20, packet->client_event_timestamp_us);
    write_u64(output + 28, packet->client_send_timestamp_us);
}

bool remote_play_decode_input_packet(RemotePlayInputPacket *packet,
                                     const uint8_t *data,
                                     size_t size)
{
    if (size != REMOTE_PLAY_INPUT_PACKET_SIZE ||
        data[0] != 'S' || data[1] != 'B' || data[2] != 'L' || data[3] != 'K' ||
        read_u16(data + 4) != REMOTE_PLAY_PROTOCOL_VERSION ||
        data[6] != INPUT_PACKET_TYPE || data[7] != 0 ||
        read_u16(data + 18) != 0) {
        return false;
    }

    uint16_t buttons = read_u16(data + 16);
    uint32_t session_id = read_u32(data + 8);
    uint64_t event_timestamp_us = read_u64(data + 20);
    uint64_t send_timestamp_us = read_u64(data + 28);
    if ((buttons & ~VALID_BUTTON_MASK) != 0 || session_id == 0 ||
        event_timestamp_us == 0 || send_timestamp_us < event_timestamp_us) {
        return false;
    }

    *packet = (RemotePlayInputPacket){
        .session_id = session_id,
        .sequence = read_u32(data + 12),
        .buttons = buttons,
        .client_event_timestamp_us = event_timestamp_us,
        .client_send_timestamp_us = send_timestamp_us,
    };
    return true;
}

void remote_play_encode_clock_sync_ping(uint8_t output[REMOTE_PLAY_CLOCK_SYNC_PING_SIZE],
                                        const RemotePlayClockSyncPing *packet)
{
    output[0] = 'S';
    output[1] = 'B';
    output[2] = 'L';
    output[3] = 'K';
    write_u16(output + 4, REMOTE_PLAY_PROTOCOL_VERSION);
    output[6] = CLOCK_SYNC_PING_PACKET_TYPE;
    output[7] = 0;
    write_u32(output + 8, packet->session_id);
    write_u32(output + 12, packet->sequence);
    write_u64(output + 16, packet->client_send_timestamp_us);
}

bool remote_play_decode_clock_sync_ping(RemotePlayClockSyncPing *packet,
                                        const uint8_t *data,
                                        size_t size)
{
    if (!packet || size != REMOTE_PLAY_CLOCK_SYNC_PING_SIZE ||
        data[0] != 'S' || data[1] != 'B' || data[2] != 'L' || data[3] != 'K' ||
        read_u16(data + 4) != REMOTE_PLAY_PROTOCOL_VERSION ||
        data[6] != CLOCK_SYNC_PING_PACKET_TYPE || data[7] != 0 ||
        read_u32(data + 8) == 0 || read_u32(data + 12) == 0 ||
        read_u64(data + 16) == 0) {
        return false;
    }

    *packet = (RemotePlayClockSyncPing){
        .session_id = read_u32(data + 8),
        .sequence = read_u32(data + 12),
        .client_send_timestamp_us = read_u64(data + 16),
    };
    return true;
}

void remote_play_encode_clock_sync_pong(uint8_t output[REMOTE_PLAY_CLOCK_SYNC_PONG_SIZE],
                                        const RemotePlayClockSyncPong *packet)
{
    output[0] = 'S';
    output[1] = 'B';
    output[2] = 'L';
    output[3] = 'K';
    write_u16(output + 4, REMOTE_PLAY_PROTOCOL_VERSION);
    output[6] = CLOCK_SYNC_PONG_PACKET_TYPE;
    output[7] = 0;
    write_u32(output + 8, packet->session_id);
    write_u32(output + 12, packet->sequence);
    write_u64(output + 16, packet->client_send_timestamp_us);
    write_u64(output + 24, packet->host_receive_timestamp_us);
    write_u64(output + 32, packet->host_send_timestamp_us);
}

bool remote_play_decode_clock_sync_pong(RemotePlayClockSyncPong *packet,
                                        const uint8_t *data,
                                        size_t size)
{
    if (!packet || size != REMOTE_PLAY_CLOCK_SYNC_PONG_SIZE ||
        data[0] != 'S' || data[1] != 'B' || data[2] != 'L' || data[3] != 'K' ||
        read_u16(data + 4) != REMOTE_PLAY_PROTOCOL_VERSION ||
        data[6] != CLOCK_SYNC_PONG_PACKET_TYPE || data[7] != 0 ||
        read_u32(data + 8) == 0 || read_u32(data + 12) == 0 ||
        read_u64(data + 16) == 0 || read_u64(data + 24) == 0 ||
        read_u64(data + 32) < read_u64(data + 24)) {
        return false;
    }

    *packet = (RemotePlayClockSyncPong){
        .session_id = read_u32(data + 8),
        .sequence = read_u32(data + 12),
        .client_send_timestamp_us = read_u64(data + 16),
        .host_receive_timestamp_us = read_u64(data + 24),
        .host_send_timestamp_us = read_u64(data + 32),
    };
    return true;
}

bool remote_play_sequence_is_newer(uint32_t candidate, uint32_t current)
{
    return (int32_t)(candidate - current) > 0;
}

size_t remote_play_encode_video_chunk(uint8_t output[REMOTE_PLAY_VIDEO_PACKET_MAX_SIZE],
                                      const RemotePlayVideoChunk *chunk)
{
    if (!chunk || !chunk->payload ||
        (chunk->pixel_format != REMOTE_PLAY_VIDEO_FORMAT_RGBA8 &&
         chunk->pixel_format != REMOTE_PLAY_VIDEO_FORMAT_RLE_RGBA8) ||
        chunk->payload_size > REMOTE_PLAY_VIDEO_CHUNK_PAYLOAD_SIZE) {
        return 0;
    }

    output[0] = 'S';
    output[1] = 'B';
    output[2] = 'L';
    output[3] = 'K';
    write_u16(output + 4, REMOTE_PLAY_PROTOCOL_VERSION);
    output[6] = VIDEO_PACKET_TYPE;
    output[7] = 0;
    write_u32(output + 8, chunk->session_id);
    write_u64(output + 12, chunk->frame_sequence);
    write_u64(output + 20, chunk->host_frame_complete_timestamp_us);
    write_u16(output + 28, chunk->width);
    write_u16(output + 30, chunk->height);
    write_u16(output + 32, chunk->chunk_index);
    write_u16(output + 34, chunk->chunk_count);
    write_u32(output + 36, chunk->payload_offset);
    write_u32(output + 40, chunk->frame_size);
    write_u16(output + 44, chunk->payload_size);
    output[46] = chunk->pixel_format;
    output[47] = 0;
    write_u32(output + 48, chunk->input_sequence);
    write_u64(output + 52, chunk->client_input_event_timestamp_us);
    write_u64(output + 60, chunk->client_input_send_timestamp_us);
    write_u64(output + 68, chunk->host_input_receive_timestamp_us);
    write_u64(output + 76, chunk->host_input_apply_timestamp_us);
    write_u64(output + 84, chunk->host_encode_begin_timestamp_us);
    write_u64(output + 92, chunk->host_encode_end_timestamp_us);
    write_u64(output + 100, chunk->host_send_timestamp_us);
    memcpy(output + REMOTE_PLAY_VIDEO_HEADER_SIZE, chunk->payload, chunk->payload_size);
    return REMOTE_PLAY_VIDEO_HEADER_SIZE + chunk->payload_size;
}

bool remote_play_decode_video_chunk(RemotePlayVideoChunk *chunk,
                                    const uint8_t *data,
                                    size_t size)
{
    if (!chunk || size < REMOTE_PLAY_VIDEO_HEADER_SIZE ||
        data[0] != 'S' || data[1] != 'B' || data[2] != 'L' || data[3] != 'K' ||
        read_u16(data + 4) != REMOTE_PLAY_PROTOCOL_VERSION ||
        data[6] != VIDEO_PACKET_TYPE || data[7] != 0 || data[47] != 0) {
        return false;
    }

    uint32_t session_id = read_u32(data + 8);
    uint16_t width = read_u16(data + 28);
    uint16_t height = read_u16(data + 30);
    uint16_t chunk_index = read_u16(data + 32);
    uint16_t chunk_count = read_u16(data + 34);
    uint32_t payload_offset = read_u32(data + 36);
    uint32_t frame_size = read_u32(data + 40);
    uint16_t payload_size = read_u16(data + 44);
    uint8_t pixel_format = data[46];
    uint32_t decoded_frame_size = (uint32_t)width * height * 4;

    if (!session_id || !width || !height ||
        width > REMOTE_PLAY_VIDEO_MAX_WIDTH || height > REMOTE_PLAY_VIDEO_MAX_HEIGHT ||
        (pixel_format != REMOTE_PLAY_VIDEO_FORMAT_RGBA8 &&
         pixel_format != REMOTE_PLAY_VIDEO_FORMAT_RLE_RGBA8) ||
        (pixel_format == REMOTE_PLAY_VIDEO_FORMAT_RGBA8 &&
         frame_size != decoded_frame_size) ||
        (pixel_format == REMOTE_PLAY_VIDEO_FORMAT_RLE_RGBA8 &&
         (!frame_size || frame_size > decoded_frame_size + (width * height + 127) / 128)) ||
        !chunk_count || chunk_count > REMOTE_PLAY_VIDEO_MAX_CHUNKS ||
        chunk_count != (frame_size + REMOTE_PLAY_VIDEO_CHUNK_PAYLOAD_SIZE - 1) /
                       REMOTE_PLAY_VIDEO_CHUNK_PAYLOAD_SIZE ||
        chunk_index >= chunk_count ||
        payload_offset != (uint32_t)chunk_index * REMOTE_PLAY_VIDEO_CHUNK_PAYLOAD_SIZE ||
        !payload_size || payload_size > REMOTE_PLAY_VIDEO_CHUNK_PAYLOAD_SIZE ||
        payload_offset + payload_size > frame_size ||
        size != REMOTE_PLAY_VIDEO_HEADER_SIZE + payload_size) {
        return false;
    }

    uint32_t expected_size = frame_size - payload_offset;
    if (expected_size > REMOTE_PLAY_VIDEO_CHUNK_PAYLOAD_SIZE) {
        expected_size = REMOTE_PLAY_VIDEO_CHUNK_PAYLOAD_SIZE;
    }
    if (payload_size != expected_size) {
        return false;
    }

    *chunk = (RemotePlayVideoChunk){
        .session_id = session_id,
        .frame_sequence = read_u64(data + 12),
        .host_frame_complete_timestamp_us = read_u64(data + 20),
        .width = width,
        .height = height,
        .chunk_index = chunk_index,
        .chunk_count = chunk_count,
        .payload_offset = payload_offset,
        .frame_size = frame_size,
        .payload_size = payload_size,
        .pixel_format = pixel_format,
        .input_sequence = read_u32(data + 48),
        .client_input_event_timestamp_us = read_u64(data + 52),
        .client_input_send_timestamp_us = read_u64(data + 60),
        .host_input_receive_timestamp_us = read_u64(data + 68),
        .host_input_apply_timestamp_us = read_u64(data + 76),
        .host_encode_begin_timestamp_us = read_u64(data + 84),
        .host_encode_end_timestamp_us = read_u64(data + 92),
        .host_send_timestamp_us = read_u64(data + 100),
        .payload = data + REMOTE_PLAY_VIDEO_HEADER_SIZE,
    };
    return true;
}

size_t remote_play_encode_audio_packet(uint8_t output[REMOTE_PLAY_AUDIO_PACKET_MAX_SIZE],
                                       const RemotePlayAudioPacket *packet)
{
    if (!packet || !packet->payload || !packet->session_id ||
        packet->sample_rate < 8000 || packet->sample_rate > 192000 ||
        !packet->frame_count || packet->frame_count > REMOTE_PLAY_AUDIO_FRAMES_PER_PACKET ||
        !packet->payload_size ||
        packet->payload_size > REMOTE_PLAY_AUDIO_PACKET_MAX_PAYLOAD_SIZE ||
        (packet->codec != REMOTE_PLAY_AUDIO_CODEC_PCM_S16LE &&
         packet->codec != REMOTE_PLAY_AUDIO_CODEC_OPUS) ||
        (packet->codec == REMOTE_PLAY_AUDIO_CODEC_PCM_S16LE &&
         packet->payload_size != packet->frame_count * REMOTE_PLAY_AUDIO_BYTES_PER_FRAME) ||
        (packet->codec == REMOTE_PLAY_AUDIO_CODEC_OPUS &&
         (packet->sample_rate != 48000 ||
          packet->frame_count != REMOTE_PLAY_AUDIO_FRAMES_PER_PACKET))) {
        return 0;
    }

    output[0] = 'S';
    output[1] = 'B';
    output[2] = 'L';
    output[3] = 'K';
    write_u16(output + 4, REMOTE_PLAY_PROTOCOL_VERSION);
    output[6] = AUDIO_PACKET_TYPE;
    output[7] = 0;
    write_u32(output + 8, packet->session_id);
    write_u32(output + 12, packet->sequence);
    write_u64(output + 16, packet->host_timestamp_us);
    write_u32(output + 24, packet->sample_rate);
    write_u16(output + 28, packet->frame_count);
    output[30] = AUDIO_CHANNELS_STEREO;
    output[31] = packet->codec;
    memcpy(output + REMOTE_PLAY_AUDIO_HEADER_SIZE,
           packet->payload,
           packet->payload_size);
    return REMOTE_PLAY_AUDIO_HEADER_SIZE + packet->payload_size;
}

bool remote_play_decode_audio_packet(RemotePlayAudioPacket *packet,
                                     const uint8_t *data,
                                     size_t size)
{
    if (!packet || size < REMOTE_PLAY_AUDIO_HEADER_SIZE ||
        data[0] != 'S' || data[1] != 'B' || data[2] != 'L' || data[3] != 'K' ||
        read_u16(data + 4) != REMOTE_PLAY_PROTOCOL_VERSION ||
        data[6] != AUDIO_PACKET_TYPE || data[7] != 0 ||
        data[30] != AUDIO_CHANNELS_STEREO) {
        return false;
    }

    uint32_t session_id = read_u32(data + 8);
    uint32_t sample_rate = read_u32(data + 24);
    uint16_t frame_count = read_u16(data + 28);
    uint8_t codec = data[31];
    size_t payload_size = size - REMOTE_PLAY_AUDIO_HEADER_SIZE;
    if (!session_id || sample_rate < 8000 || sample_rate > 192000 ||
        !frame_count || frame_count > REMOTE_PLAY_AUDIO_FRAMES_PER_PACKET ||
        !payload_size || payload_size > REMOTE_PLAY_AUDIO_PACKET_MAX_PAYLOAD_SIZE ||
        (codec != REMOTE_PLAY_AUDIO_CODEC_PCM_S16LE &&
         codec != REMOTE_PLAY_AUDIO_CODEC_OPUS) ||
        (codec == REMOTE_PLAY_AUDIO_CODEC_PCM_S16LE &&
         payload_size != (size_t)frame_count * REMOTE_PLAY_AUDIO_BYTES_PER_FRAME) ||
        (codec == REMOTE_PLAY_AUDIO_CODEC_OPUS &&
         (sample_rate != 48000 ||
          frame_count != REMOTE_PLAY_AUDIO_FRAMES_PER_PACKET))) {
        return false;
    }

    *packet = (RemotePlayAudioPacket){
        .session_id = session_id,
        .sequence = read_u32(data + 12),
        .host_timestamp_us = read_u64(data + 16),
        .sample_rate = sample_rate,
        .frame_count = frame_count,
        .payload_size = (uint16_t)payload_size,
        .codec = codec,
        .payload = data + REMOTE_PLAY_AUDIO_HEADER_SIZE,
    };
    return true;
}
