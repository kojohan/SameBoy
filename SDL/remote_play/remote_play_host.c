#include "remote_play_host.h"

#include <SDL.h>
#include <string.h>

#ifdef ENABLE_REMOTE_OPUS
#include <opus/opus.h>
#endif

#include "../link_diagnostics.h"
#include "../session/multiplayer_input.h"
#include "protocol.h"
#include "video_codec.h"

#define REMOTE_INPUT_TIMEOUT_US 500000

static uint64_t monotonic_time_us(void)
{
    uint64_t counter = SDL_GetPerformanceCounter();
    uint64_t frequency = SDL_GetPerformanceFrequency();
    return counter / frequency * 1000000 + counter % frequency * 1000000 / frequency;
}

static void reset_audio_queue(RemotePlayHost *host)
{
    host->audio_current_frames = 0;
    host->audio_queue_head = 0;
    host->audio_queue_count = 0;
}

static const char *handshake_status_name(RemotePlayHandshakeStatus status)
{
    switch (status) {
        case REMOTE_PLAY_HANDSHAKE_ACCEPTED: return "accepted";
        case REMOTE_PLAY_HANDSHAKE_SESSION_MISMATCH: return "session_mismatch";
        case REMOTE_PLAY_HANDSHAKE_PROTOCOL_MISMATCH: return "protocol_mismatch";
    }
    return "unknown";
}

bool remote_play_host_start(RemotePlayHost *host,
                            GameSession *session,
                            uint16_t port,
                            uint32_t session_id,
                            char *error,
                            size_t error_size)
{
    memset(host, 0, sizeof(*host));
    if (session_id == 0 || !remote_udp_open_host(&host->transport, port, error, error_size)) {
        return false;
    }

    host->session = session;
    host->session_id = session_id;
    host->port = port;
    host->host_id = monotonic_time_us();
    if (!host->host_id) host->host_id = 1;
    host->audio_codec = REMOTE_PLAY_AUDIO_CODEC_PCM_S16LE;
    host->active = true;
    sameboy_link_log(SAMEBOY_LINK_LOG_FRONTEND,
                     "remote_input_host listening_udp_port=%u session=%u protocol=%u",
                     port,
                     session_id,
                     REMOTE_PLAY_PROTOCOL_VERSION);
    return true;
}

void remote_play_host_poll(RemotePlayHost *host)
{
    if (!host->active) {
        return;
    }

    uint8_t data[256];
    char error[128];
    for (;;) {
        RemoteUdpEndpoint sender;
        int size = remote_udp_receive(&host->transport,
                                      data,
                                      sizeof(data),
                                      &sender,
                                      error,
                                      sizeof(error));
        if (size == 0) {
            break;
        }
        if (size < 0) {
            host->rejected_packets++;
            sameboy_link_log(SAMEBOY_LINK_LOG_FRONTEND, "remote_input_host receive_error=%s", error);
            break;
        }

        uint64_t receive_time = monotonic_time_us();
        RemotePlayHandshakeHello hello;
        if (remote_play_decode_handshake_hello(&hello, data, size)) {
            RemotePlayHandshakeStatus status = REMOTE_PLAY_HANDSHAKE_ACCEPTED;
            if (hello.protocol_version != REMOTE_PLAY_PROTOCOL_VERSION) {
                status = REMOTE_PLAY_HANDSHAKE_PROTOCOL_MISMATCH;
            }
            else if (hello.session_id != host->session_id) {
                status = REMOTE_PLAY_HANDSHAKE_SESSION_MISMATCH;
            }

            if (status == REMOTE_PLAY_HANDSHAKE_ACCEPTED) {
                bool peer_changed = !host->handshake_complete ||
                    !remote_udp_endpoint_equal(&sender, &host->transport.peer);
                if (peer_changed && host->client_connected) {
                    multiplayer_input_apply_button_mask(host->session, 0);
                    host->last_buttons = 0;
                    host->client_connected = false;
                    host->has_sequence = false;
                    reset_audio_queue(host);
                }
                remote_udp_set_peer(&host->transport, &sender);
                host->handshake_complete = true;
            }

            RemotePlayHandshakeResponse response = {
                .protocol_version = REMOTE_PLAY_PROTOCOL_VERSION,
                .session_id = hello.session_id,
                .request_id = hello.request_id,
                .host_id = host->host_id,
                .status = status,
            };
            uint8_t encoded[REMOTE_PLAY_HANDSHAKE_RESPONSE_SIZE];
            remote_play_encode_handshake_response(encoded, &response);
            if (remote_udp_send_to(&host->transport,
                                   &sender,
                                   encoded,
                                   sizeof(encoded),
                                   error,
                                   sizeof(error))) {
                host->handshake_responses++;
            }
            else {
                sameboy_link_log(SAMEBOY_LINK_LOG_FRONTEND,
                                 "remote_handshake_host send_error=%s",
                                 error);
            }
            host->handshake_requests++;
            if (!host->has_handshake_status ||
                status != host->last_handshake_status) {
                sameboy_link_log(SAMEBOY_LINK_LOG_FRONTEND,
                                 "remote_handshake_host status=%s client_protocol=%u host_protocol=%u requested_session=%u",
                                 handshake_status_name(status),
                                 hello.protocol_version,
                                 REMOTE_PLAY_PROTOCOL_VERSION,
                                 hello.session_id);
                host->last_handshake_status = status;
                host->has_handshake_status = true;
            }
            continue;
        }

        RemotePlayClockSyncPing ping;
        if (remote_play_decode_clock_sync_ping(&ping, data, size) &&
            ping.session_id == host->session_id &&
            host->handshake_complete &&
            remote_udp_endpoint_equal(&sender, &host->transport.peer)) {
            RemotePlayClockSyncPong pong = {
                .session_id = host->session_id,
                .sequence = ping.sequence,
                .client_send_timestamp_us = ping.client_send_timestamp_us,
                .host_receive_timestamp_us = receive_time,
                .host_send_timestamp_us = monotonic_time_us(),
            };
            uint8_t response[REMOTE_PLAY_CLOCK_SYNC_PONG_SIZE];
            remote_play_encode_clock_sync_pong(response, &pong);
            if (remote_udp_send(&host->transport,
                                response,
                                sizeof(response),
                                error,
                                sizeof(error))) {
                host->clock_sync_pongs++;
            }
            else {
                sameboy_link_log(SAMEBOY_LINK_LOG_TIMING,
                                 "remote_clock_sync_host send_error=%s",
                                 error);
            }
            host->clock_sync_pings++;
            continue;
        }

        RemotePlayInputPacket packet;
        if (!remote_play_decode_input_packet(&packet, data, size) ||
            packet.session_id != host->session_id ||
            !host->handshake_complete ||
            !remote_udp_endpoint_equal(&sender, &host->transport.peer)) {
            host->rejected_packets++;
            continue;
        }
        if (host->has_sequence &&
            !remote_play_sequence_is_newer(packet.sequence, host->last_sequence)) {
            host->stale_packets++;
            continue;
        }

        uint16_t previous_buttons = host->last_buttons;
        uint64_t receive_gap = host->last_receive_time_us?
            receive_time - host->last_receive_time_us : 0;
        multiplayer_input_apply_button_mask(host->session, packet.buttons);
        uint64_t apply_time = monotonic_time_us();
        host->last_sequence = packet.sequence;
        host->last_buttons = packet.buttons;
        host->last_receive_time_us = receive_time;
        host->last_input_gap_us = receive_gap;
        if (receive_gap > host->max_input_gap_us) {
            host->max_input_gap_us = receive_gap;
        }
        host->accepted_packets++;
        host->has_sequence = true;
        if (!host->client_connected) {
            reset_audio_queue(host);
            host->client_connected = true;
            host->client_connected_notice_pending = true;
            sameboy_link_log(SAMEBOY_LINK_LOG_FRONTEND,
                             "remote_input_host client_active first_sequence=%u",
                             packet.sequence);
        }
        if (packet.buttons != previous_buttons) {
            host->changed_input_sequence = packet.sequence;
            host->client_input_event_timestamp_us = packet.client_event_timestamp_us;
            host->client_input_send_timestamp_us = packet.client_send_timestamp_us;
            host->host_input_receive_timestamp_us = receive_time;
            host->host_input_apply_timestamp_us = apply_time;
            sameboy_link_log(SAMEBOY_LINK_LOG_FRONTEND,
                             "remote_input_host applied sequence=%u buttons=0x%02x receive_gap_us=%llu receive_to_apply_us=%llu",
                             packet.sequence,
                             packet.buttons,
                             (unsigned long long)host->last_input_gap_us,
                             (unsigned long long)(apply_time - receive_time));
        }
    }

    uint64_t now = monotonic_time_us();
    if (host->client_connected && now - host->last_receive_time_us > REMOTE_INPUT_TIMEOUT_US) {
        multiplayer_input_apply_button_mask(host->session, 0);
        host->last_buttons = 0;
        host->client_connected = false;
        host->client_disconnected_notice_pending = true;
        reset_audio_queue(host);
        sameboy_link_log(SAMEBOY_LINK_LOG_FRONTEND,
                         "remote_input_host client_timeout timeout_ms=%u",
                         REMOTE_INPUT_TIMEOUT_US / 1000);
    }
}

bool remote_play_host_take_client_connected_notice(RemotePlayHost *host)
{
    if (!host || !host->client_connected_notice_pending) {
        return false;
    }
    host->client_connected_notice_pending = false;
    return true;
}

bool remote_play_host_take_client_disconnected_notice(RemotePlayHost *host)
{
    if (!host || !host->client_disconnected_notice_pending) {
        return false;
    }
    host->client_disconnected_notice_pending = false;
    return true;
}

void remote_play_host_set_audio_sample_rate(RemotePlayHost *host, uint32_t sample_rate)
{
    if (host->active) {
        host->audio_sample_rate = sample_rate;
    }
}

void remote_play_host_set_audio_codec(RemotePlayHost *host, uint8_t codec)
{
    if (codec != REMOTE_PLAY_AUDIO_CODEC_PCM_S16LE &&
        codec != REMOTE_PLAY_AUDIO_CODEC_OPUS) {
        codec = REMOTE_PLAY_AUDIO_CODEC_PCM_S16LE;
    }
#ifdef ENABLE_REMOTE_OPUS
    if (host->audio_encoder) {
        opus_encoder_destroy(host->audio_encoder);
        host->audio_encoder = NULL;
    }
#endif
    host->audio_codec = codec;
}

static bool ensure_audio_encoder(RemotePlayHost *host)
{
    if (host->audio_codec != REMOTE_PLAY_AUDIO_CODEC_OPUS) {
        return true;
    }
#ifdef ENABLE_REMOTE_OPUS
    if (host->audio_encoder) {
        return true;
    }
    if (host->audio_sample_rate != 48000) {
        sameboy_link_log(SAMEBOY_LINK_LOG_AUDIO,
                         "remote_audio_host opus_error=sample rate must be 48000");
        return false;
    }

    int error = OPUS_OK;
    OpusEncoder *encoder = opus_encoder_create(48000,
                                                2,
                                                OPUS_APPLICATION_RESTRICTED_LOWDELAY,
                                                &error);
    if (!encoder || error != OPUS_OK) {
        sameboy_link_log(SAMEBOY_LINK_LOG_AUDIO,
                         "remote_audio_host opus_create_error=%s",
                         opus_strerror(error));
        if (encoder) opus_encoder_destroy(encoder);
        return false;
    }
    if (opus_encoder_ctl(encoder, OPUS_SET_BITRATE(96000)) != OPUS_OK ||
        opus_encoder_ctl(encoder, OPUS_SET_VBR(1)) != OPUS_OK ||
        opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(5)) != OPUS_OK ||
        opus_encoder_ctl(encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC)) != OPUS_OK) {
        sameboy_link_log(SAMEBOY_LINK_LOG_AUDIO,
                         "remote_audio_host opus_error=encoder configuration failed");
        opus_encoder_destroy(encoder);
        return false;
    }
    int lookahead = 0;
    opus_encoder_ctl(encoder, OPUS_GET_LOOKAHEAD(&lookahead));
    host->audio_lookahead_frames = lookahead > 0? (uint16_t)lookahead : 0;
    host->audio_encoder = encoder;
    sameboy_link_log(SAMEBOY_LINK_LOG_AUDIO,
                     "remote_audio_host codec=opus application=restricted_lowdelay bitrate=96000 frame_ms=5 complexity=5 lookahead_frames=%u lookahead_ms=%.3f",
                     host->audio_lookahead_frames,
                     host->audio_lookahead_frames / 48.0);
    return true;
#else
    sameboy_link_log(SAMEBOY_LINK_LOG_AUDIO,
                     "remote_audio_host opus_error=build has no Opus support");
    return false;
#endif
}

static void write_s16le(uint8_t *output, int16_t value)
{
    uint16_t encoded = (uint16_t)value;
    output[0] = encoded;
    output[1] = encoded >> 8;
}

void remote_play_host_queue_audio_sample(RemotePlayHost *host, int16_t left, int16_t right)
{
    if (!host->active || !host->client_connected || !host->audio_sample_rate) {
        return;
    }

    uint8_t *output = host->audio_current +
                      host->audio_current_frames * REMOTE_PLAY_AUDIO_BYTES_PER_FRAME;
    write_s16le(output, left);
    write_s16le(output + 2, right);
    host->audio_current_frames++;
    if (host->audio_current_frames < REMOTE_PLAY_AUDIO_FRAMES_PER_PACKET) {
        return;
    }

    if (host->audio_queue_count == REMOTE_PLAY_AUDIO_QUEUE_CAPACITY) {
        host->audio_queue_head =
            (host->audio_queue_head + 1) % REMOTE_PLAY_AUDIO_QUEUE_CAPACITY;
        host->audio_queue_count--;
        host->audio_packets_dropped++;
    }
    unsigned tail = (host->audio_queue_head + host->audio_queue_count) %
                    REMOTE_PLAY_AUDIO_QUEUE_CAPACITY;
    memcpy(host->audio_queue[tail], host->audio_current, sizeof(host->audio_current));
    host->audio_queue_count++;
    host->audio_current_frames = 0;
}

void remote_play_host_send_pending_audio(RemotePlayHost *host)
{
    if (!host->active || !host->client_connected || !host->audio_sample_rate) {
        reset_audio_queue(host);
        return;
    }

    uint8_t encoded[REMOTE_PLAY_AUDIO_PACKET_MAX_SIZE];
#ifdef ENABLE_REMOTE_OPUS
    uint8_t opus_payload[REMOTE_PLAY_AUDIO_PACKET_MAX_PAYLOAD_SIZE];
#endif
    while (host->audio_queue_count) {
        const uint8_t *payload = host->audio_queue[host->audio_queue_head];
        uint16_t payload_size = REMOTE_PLAY_AUDIO_PCM_PAYLOAD_SIZE;
        uint64_t encode_begin_us = monotonic_time_us();
        if (host->audio_codec == REMOTE_PLAY_AUDIO_CODEC_OPUS) {
            if (!ensure_audio_encoder(host)) {
                host->audio_packets_dropped += host->audio_queue_count;
                reset_audio_queue(host);
                return;
            }
#ifdef ENABLE_REMOTE_OPUS
            int16_t pcm[REMOTE_PLAY_AUDIO_FRAMES_PER_PACKET * 2];
            for (unsigned sample = 0;
                 sample < REMOTE_PLAY_AUDIO_FRAMES_PER_PACKET * 2;
                 sample++) {
                const uint8_t *source = payload + sample * sizeof(int16_t);
                pcm[sample] = (int16_t)((uint16_t)source[0] |
                                        (uint16_t)source[1] << 8);
            }
            int result = opus_encode(host->audio_encoder,
                                     pcm,
                                     REMOTE_PLAY_AUDIO_FRAMES_PER_PACKET,
                                     opus_payload,
                                     sizeof(opus_payload));
            if (result <= 0) {
                host->audio_packets_dropped++;
                sameboy_link_log(SAMEBOY_LINK_LOG_AUDIO,
                                 "remote_audio_host opus_encode_error=%s",
                                 opus_strerror(result));
                host->audio_queue_head =
                    (host->audio_queue_head + 1) % REMOTE_PLAY_AUDIO_QUEUE_CAPACITY;
                host->audio_queue_count--;
                continue;
            }
            payload = opus_payload;
            payload_size = (uint16_t)result;
#endif
        }
        host->audio_encode_time_us += monotonic_time_us() - encode_begin_us;
        RemotePlayAudioPacket packet = {
            .session_id = host->session_id,
            .sequence = ++host->audio_sequence,
            .host_timestamp_us = monotonic_time_us(),
            .sample_rate = host->audio_sample_rate,
            .frame_count = REMOTE_PLAY_AUDIO_FRAMES_PER_PACKET,
            .payload_size = payload_size,
            .codec = host->audio_codec,
            .payload = payload,
        };
        size_t encoded_size = remote_play_encode_audio_packet(encoded, &packet);
        char error[128];
        if (!encoded_size ||
            !remote_udp_send(&host->transport, encoded, encoded_size, error, sizeof(error))) {
            host->audio_packets_dropped += host->audio_queue_count;
            if (host->audio_packets_dropped == host->audio_queue_count) {
                sameboy_link_log(SAMEBOY_LINK_LOG_AUDIO,
                                 "remote_audio_host first_drop error=%s",
                                 encoded_size? error : "encode failed");
            }
            reset_audio_queue(host);
            return;
        }

        host->audio_queue_head =
            (host->audio_queue_head + 1) % REMOTE_PLAY_AUDIO_QUEUE_CAPACITY;
        host->audio_queue_count--;
        host->audio_packets_sent++;
        host->audio_frames_sent += packet.frame_count;
        host->audio_payload_bytes += packet.payload_size;
        if (host->audio_packets_sent == 1) {
            sameboy_link_log(SAMEBOY_LINK_LOG_AUDIO,
                             "remote_audio_host first_packet sequence=%u rate=%u frames=%u codec=%s payload_bytes=%u",
                             packet.sequence,
                             packet.sample_rate,
                             packet.frame_count,
                             packet.codec == REMOTE_PLAY_AUDIO_CODEC_OPUS?
                                "opus" : "pcm_s16le",
                             packet.payload_size);
        }
        else if (host->audio_packets_sent % 600 == 0) {
            sameboy_link_log(SAMEBOY_LINK_LOG_AUDIO,
                             "remote_audio_host packets=%llu dropped=%llu frames=%llu payload_bytes=%llu average_payload_bytes=%.1f average_encode_us=%.2f",
                             (unsigned long long)host->audio_packets_sent,
                             (unsigned long long)host->audio_packets_dropped,
                             (unsigned long long)host->audio_frames_sent,
                             (unsigned long long)host->audio_payload_bytes,
                             (double)host->audio_payload_bytes / host->audio_packets_sent,
                             (double)host->audio_encode_time_us /
                                host->audio_packets_sent);
        }
    }
}

static void encode_rgba_frame(RemotePlayHost *host,
                              const GameSessionFrame *frame,
                              const SDL_PixelFormat *format)
{
    size_t pixel_count = frame->width * frame->height;
    if (format->format == SDL_PIXELFORMAT_ABGR8888) {
        memcpy(host->video_frame, frame->pixels, pixel_count * sizeof(uint32_t));
        return;
    }

    for (size_t i = 0; i < pixel_count; i++) {
        uint8_t *output = &host->video_frame[i * 4];
        SDL_GetRGBA(frame->pixels[i], format, output, output + 1, output + 2, output + 3);
    }
}

void remote_play_host_send_completed_frame(RemotePlayHost *host,
                                           const GameSessionFrame *frame,
                                           const SDL_PixelFormat *format)
{
    if (!host->active || !host->client_connected || !frame || !format ||
        frame->sequence == host->last_video_sequence ||
        frame->width > REMOTE_PLAY_VIDEO_MAX_WIDTH ||
        frame->height > REMOTE_PLAY_VIDEO_MAX_HEIGHT) {
        return;
    }

    host->last_video_sequence = frame->sequence;
    uint64_t stream_sequence = ++host->video_stream_sequence;
    uint64_t encode_begin_time = monotonic_time_us();
    encode_rgba_frame(host, frame, format);
    uint32_t raw_frame_size = frame->width * frame->height * 4;
    size_t rle_size = remote_video_rle_encode_rgba8(host->video_encoded_frame,
                                                     sizeof(host->video_encoded_frame),
                                                     host->video_frame,
                                                     frame->width * frame->height);
    const uint8_t *encoded_frame = host->video_frame;
    uint32_t encoded_frame_size = raw_frame_size;
    uint8_t pixel_format = REMOTE_PLAY_VIDEO_FORMAT_RGBA8;
    if (rle_size && rle_size < raw_frame_size) {
        encoded_frame = host->video_encoded_frame;
        encoded_frame_size = (uint32_t)rle_size;
        pixel_format = REMOTE_PLAY_VIDEO_FORMAT_RLE_RGBA8;
    }
    uint64_t encode_end_time = monotonic_time_us();
    uint16_t chunk_count = (encoded_frame_size + REMOTE_PLAY_VIDEO_CHUNK_PAYLOAD_SIZE - 1) /
                           REMOTE_PLAY_VIDEO_CHUNK_PAYLOAD_SIZE;
    uint64_t send_time = monotonic_time_us();
    uint8_t encoded[REMOTE_PLAY_VIDEO_PACKET_MAX_SIZE];

    for (uint16_t chunk_index = 0; chunk_index < chunk_count; chunk_index++) {
        uint32_t offset = chunk_index * REMOTE_PLAY_VIDEO_CHUNK_PAYLOAD_SIZE;
        uint32_t remaining = encoded_frame_size - offset;
        uint16_t payload_size = remaining > REMOTE_PLAY_VIDEO_CHUNK_PAYLOAD_SIZE?
            REMOTE_PLAY_VIDEO_CHUNK_PAYLOAD_SIZE : (uint16_t)remaining;

        RemotePlayVideoChunk chunk = {
            .session_id = host->session_id,
            .frame_sequence = stream_sequence,
            .host_frame_complete_timestamp_us = frame->completed_timestamp_us,
            .width = frame->width,
            .height = frame->height,
            .chunk_index = chunk_index,
            .chunk_count = chunk_count,
            .payload_offset = offset,
            .frame_size = encoded_frame_size,
            .payload_size = payload_size,
            .pixel_format = pixel_format,
            .input_sequence = host->changed_input_sequence,
            .client_input_event_timestamp_us = host->client_input_event_timestamp_us,
            .client_input_send_timestamp_us = host->client_input_send_timestamp_us,
            .host_input_receive_timestamp_us = host->host_input_receive_timestamp_us,
            .host_input_apply_timestamp_us = host->host_input_apply_timestamp_us,
            .host_encode_begin_timestamp_us = encode_begin_time,
            .host_encode_end_timestamp_us = encode_end_time,
            .host_send_timestamp_us = send_time,
            .payload = encoded_frame + offset,
        };
        size_t encoded_size = remote_play_encode_video_chunk(encoded, &chunk);
        char error[128];
        if (!encoded_size ||
            !remote_udp_send(&host->transport, encoded, encoded_size, error, sizeof(error))) {
            host->video_frames_dropped++;
            if (host->video_frames_dropped == 1) {
                sameboy_link_log(SAMEBOY_LINK_LOG_VIDEO,
                                 "remote_video_host first_drop error=%s",
                                 encoded_size? error : "encode failed");
            }
            return;
        }
        host->video_chunks_sent++;
    }

    uint64_t burst_time_us = monotonic_time_us() - send_time;
    host->video_frames_sent++;
    host->video_raw_bytes += raw_frame_size;
    host->video_encoded_bytes += encoded_frame_size;
    host->video_burst_time_us += burst_time_us;
    if (burst_time_us > host->video_maximum_burst_us) {
        host->video_maximum_burst_us = burst_time_us;
    }
    if (burst_time_us >= 5000) {
        host->video_slow_bursts++;
    }
    if (chunk_count > host->video_maximum_chunks_per_frame) {
        host->video_maximum_chunks_per_frame = chunk_count;
    }
    if (host->video_frames_sent == 1) {
        sameboy_link_log(SAMEBOY_LINK_LOG_VIDEO,
                         "remote_video_host first_stream_frame=%llu source_frame=%llu size=%ux%u raw_bytes=%u encoded_bytes=%u chunks=%u burst_us=%llu format=%s",
                         (unsigned long long)stream_sequence,
                         (unsigned long long)frame->sequence,
                         frame->width,
                         frame->height,
                         raw_frame_size,
                         encoded_frame_size,
                         chunk_count,
                         (unsigned long long)burst_time_us,
                         pixel_format == REMOTE_PLAY_VIDEO_FORMAT_RLE_RGBA8? "RLE_RGBA8" : "RGBA8");
    }
    else if (host->video_frames_sent % 600 == 0) {
        sameboy_link_log(SAMEBOY_LINK_LOG_VIDEO,
                         "remote_video_host frames=%llu dropped=%llu chunks=%llu encoded_ratio=%.3f average_burst_us=%.1f maximum_burst_us=%llu slow_bursts=%llu maximum_chunks_per_frame=%u",
                         (unsigned long long)host->video_frames_sent,
                         (unsigned long long)host->video_frames_dropped,
                         (unsigned long long)host->video_chunks_sent,
                         host->video_raw_bytes?
                             (double)host->video_encoded_bytes / host->video_raw_bytes : 0.0,
                         (double)host->video_burst_time_us / host->video_frames_sent,
                         (unsigned long long)host->video_maximum_burst_us,
                         (unsigned long long)host->video_slow_bursts,
                         host->video_maximum_chunks_per_frame);
    }
}

void remote_play_host_stop(RemotePlayHost *host)
{
    if (!host->active) {
        return;
    }

    multiplayer_input_apply_button_mask(host->session, 0);
    remote_udp_close(&host->transport);
    sameboy_link_log(SAMEBOY_LINK_LOG_FRONTEND,
                     "remote_input_host stopped accepted=%llu rejected=%llu stale=%llu last_sequence=%u last_receive_gap_us=%llu max_receive_gap_us=%llu",
                     (unsigned long long)host->accepted_packets,
                     (unsigned long long)host->rejected_packets,
                     (unsigned long long)host->stale_packets,
                     host->last_sequence,
                     (unsigned long long)host->last_input_gap_us,
                     (unsigned long long)host->max_input_gap_us);
    sameboy_link_log(SAMEBOY_LINK_LOG_FRONTEND,
                     "remote_handshake_host stopped requests=%llu responses=%llu completed=%s",
                     (unsigned long long)host->handshake_requests,
                     (unsigned long long)host->handshake_responses,
                     host->handshake_complete? "yes" : "no");
    sameboy_link_log(SAMEBOY_LINK_LOG_VIDEO,
                     "remote_video_host stopped frames_sent=%llu frames_dropped=%llu chunks_sent=%llu raw_bytes=%llu encoded_bytes=%llu encoded_ratio=%.3f average_burst_us=%.1f maximum_burst_us=%llu slow_bursts=%llu maximum_chunks_per_frame=%u",
                     (unsigned long long)host->video_frames_sent,
                     (unsigned long long)host->video_frames_dropped,
                     (unsigned long long)host->video_chunks_sent,
                     (unsigned long long)host->video_raw_bytes,
                     (unsigned long long)host->video_encoded_bytes,
                     host->video_raw_bytes?
                         (double)host->video_encoded_bytes / host->video_raw_bytes : 0.0,
                     host->video_frames_sent?
                         (double)host->video_burst_time_us / host->video_frames_sent : 0.0,
                     (unsigned long long)host->video_maximum_burst_us,
                     (unsigned long long)host->video_slow_bursts,
                     host->video_maximum_chunks_per_frame);
    sameboy_link_log(SAMEBOY_LINK_LOG_AUDIO,
                     "remote_audio_host stopped codec=%s packets_sent=%llu packets_dropped=%llu frames_sent=%llu payload_bytes=%llu average_payload_bytes=%.1f average_encode_us=%.2f payload_bitrate_kbps=%.1f",
                     host->audio_codec == REMOTE_PLAY_AUDIO_CODEC_OPUS?
                        "opus" : "pcm_s16le",
                     (unsigned long long)host->audio_packets_sent,
                     (unsigned long long)host->audio_packets_dropped,
                     (unsigned long long)host->audio_frames_sent,
                     (unsigned long long)host->audio_payload_bytes,
                     host->audio_packets_sent?
                        (double)host->audio_payload_bytes / host->audio_packets_sent : 0.0,
                     host->audio_packets_sent?
                        (double)host->audio_encode_time_us / host->audio_packets_sent : 0.0,
                     host->audio_frames_sent?
                        host->audio_payload_bytes * 8.0 * host->audio_sample_rate /
                            host->audio_frames_sent / 1000.0 : 0.0);
    sameboy_link_log(SAMEBOY_LINK_LOG_TIMING,
                     "remote_clock_sync_host stopped pings=%llu pongs=%llu",
                     (unsigned long long)host->clock_sync_pings,
                     (unsigned long long)host->clock_sync_pongs);
#ifdef ENABLE_REMOTE_OPUS
    if (host->audio_encoder) {
        opus_encoder_destroy(host->audio_encoder);
        host->audio_encoder = NULL;
    }
#endif
    host->active = false;
}
