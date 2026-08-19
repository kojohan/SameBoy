#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <SDL.h>

#include "../session/game_session.h"
#include "protocol.h"
#include "transport_udp.h"

#define REMOTE_PLAY_AUDIO_QUEUE_CAPACITY 8

typedef struct {
    RemoteUdpSocket transport;
    GameSession *session;
    uint32_t session_id;
    uint32_t last_sequence;
    uint16_t last_buttons;
    uint64_t last_receive_time_us;
    uint64_t last_input_gap_us;
    uint64_t max_input_gap_us;
    uint32_t changed_input_sequence;
    uint64_t client_input_event_timestamp_us;
    uint64_t client_input_send_timestamp_us;
    uint64_t host_input_receive_timestamp_us;
    uint64_t host_input_apply_timestamp_us;
    uint64_t accepted_packets;
    uint64_t rejected_packets;
    uint64_t stale_packets;
    uint64_t clock_sync_pings;
    uint64_t clock_sync_pongs;
    uint64_t handshake_requests;
    uint64_t handshake_responses;
    uint64_t host_id;
    uint64_t last_video_sequence;
    uint64_t video_stream_sequence;
    uint64_t video_frames_sent;
    uint64_t video_frames_dropped;
    uint64_t video_chunks_sent;
    uint64_t video_raw_bytes;
    uint64_t video_encoded_bytes;
    uint64_t video_burst_time_us;
    uint64_t video_maximum_burst_us;
    uint64_t video_slow_bursts;
    uint16_t video_maximum_chunks_per_frame;
    uint64_t audio_packets_sent;
    uint64_t audio_packets_dropped;
    uint64_t audio_frames_sent;
    uint64_t audio_payload_bytes;
    uint64_t audio_encode_time_us;
    uint8_t video_frame[REMOTE_PLAY_VIDEO_MAX_FRAME_SIZE];
    uint8_t video_encoded_frame[REMOTE_PLAY_VIDEO_MAX_ENCODED_SIZE];
    uint8_t audio_current[REMOTE_PLAY_AUDIO_PCM_PAYLOAD_SIZE];
    uint8_t audio_queue[REMOTE_PLAY_AUDIO_QUEUE_CAPACITY][REMOTE_PLAY_AUDIO_PCM_PAYLOAD_SIZE];
    void *audio_encoder;
    uint32_t audio_sample_rate;
    uint32_t audio_sequence;
    uint16_t audio_lookahead_frames;
    uint16_t audio_current_frames;
    uint8_t audio_queue_head;
    uint8_t audio_queue_count;
    uint8_t audio_codec;
    RemotePlayHandshakeStatus last_handshake_status;
    uint16_t port;
    bool active;
    bool has_sequence;
    bool has_handshake_status;
    bool handshake_complete;
    bool client_connected;
    bool client_connected_notice_pending;
    bool client_disconnected_notice_pending;
} RemotePlayHost;

bool remote_play_host_start(RemotePlayHost *host,
                            GameSession *session,
                            uint16_t port,
                            uint32_t session_id,
                            char *error,
                            size_t error_size);
void remote_play_host_poll(RemotePlayHost *host);
bool remote_play_host_take_client_connected_notice(RemotePlayHost *host);
bool remote_play_host_take_client_disconnected_notice(RemotePlayHost *host);
void remote_play_host_send_completed_frame(RemotePlayHost *host,
                                           const GameSessionFrame *frame,
                                           const SDL_PixelFormat *format);
void remote_play_host_set_audio_sample_rate(RemotePlayHost *host, uint32_t sample_rate);
void remote_play_host_set_audio_codec(RemotePlayHost *host, uint8_t codec);
void remote_play_host_queue_audio_sample(RemotePlayHost *host, int16_t left, int16_t right);
void remote_play_host_send_pending_audio(RemotePlayHost *host);
void remote_play_host_stop(RemotePlayHost *host);
