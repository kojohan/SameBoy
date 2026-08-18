#include "remote_play_client.h"

#include <SDL.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#ifdef ENABLE_REMOTE_OPUS
#include <opus/opus.h>
#endif

#include "../session/multiplayer_input.h"
#include "../controller_input.h"
#include "../gui.h"
#include "protocol.h"
#include "transport_udp.h"
#include "video_codec.h"

#define INPUT_KEEPALIVE_US 50000
#define CLOCK_SYNC_INTERVAL_US 500000
#define REMOTE_AUDIO_INITIAL_TARGET_MS 40
#define REMOTE_AUDIO_MIN_TARGET_MS 40
#define REMOTE_AUDIO_MAX_TARGET_MS 100
#define REMOTE_AUDIO_MAX_QUEUE_MS 120
#define REMOTE_AUDIO_TARGET_STEP_MS 5
#define REMOTE_AUDIO_TARGET_DECAY_US 30000000
#define REMOTE_AUDIO_DRIFT_GAIN 0.05
#define REMOTE_AUDIO_MAX_RATE_CORRECTION 0.002

static uint64_t client_start_time_us;

static void set_button_state(uint16_t *buttons, uint16_t button, bool pressed)
{
    if (pressed) {
        *buttons |= button;
    }
    else {
        *buttons &= ~button;
    }
}

typedef struct {
    uint32_t input_sequence;
    uint64_t client_input_event_timestamp_us;
    uint64_t client_input_send_timestamp_us;
    uint64_t host_input_receive_timestamp_us;
    uint64_t host_input_apply_timestamp_us;
    uint64_t host_frame_complete_timestamp_us;
    uint64_t host_encode_begin_timestamp_us;
    uint64_t host_encode_end_timestamp_us;
    uint64_t host_send_timestamp_us;
    uint64_t client_first_receive_timestamp_us;
    uint64_t client_last_receive_timestamp_us;
    uint64_t client_decode_begin_timestamp_us;
    uint64_t client_decode_end_timestamp_us;
} RemoteFrameTelemetry;

typedef struct {
    uint32_t sequence;
    uint32_t last_response_sequence;
    uint64_t last_send_timestamp_us;
    uint64_t responses;
    uint64_t min_rtt_us;
    double smoothed_rtt_us;
    double jitter_us;
    int64_t host_minus_client_us;
    bool synchronized;
} RemoteClockSync;

typedef struct {
    uint32_t last_input_sequence;
    uint64_t samples;
    double latest_total_ms;
    double average_total_ms;
    double maximum_total_ms;
} RemoteLatencyStats;

typedef struct {
    uint8_t frame_buffers[2][REMOTE_PLAY_VIDEO_MAX_ENCODED_SIZE];
    bool chunks_received[REMOTE_PLAY_VIDEO_MAX_CHUNKS];
    uint64_t assembling_sequence;
    uint64_t completed_sequence;
    uint64_t highest_seen_sequence;
    uint64_t frames_completed;
    uint64_t frames_dropped;
    uint64_t packets_rejected;
    uint64_t raw_bytes_completed;
    uint64_t encoded_bytes_completed;
    uint64_t frames_superseded_before_present;
    uint16_t assembling_width;
    uint16_t assembling_height;
    uint16_t assembling_chunk_count;
    uint16_t received_chunk_count;
    uint32_t assembling_frame_size;
    uint8_t assembling_pixel_format;
    RemoteFrameTelemetry assembling_telemetry;
    uint16_t completed_width;
    uint16_t completed_height;
    unsigned assembly_buffer;
    unsigned completed_buffer;
    RemoteFrameTelemetry completed_telemetry;
    bool has_assembly;
    bool frame_ready;
} RemoteVideoReceiver;

typedef struct {
    SDL_AudioDeviceID device;
    int16_t *ring_samples;
    void *decoder;
    uint32_t sample_rate;
    uint32_t ring_capacity_frames;
    uint32_t ring_read_frame;
    uint32_t ring_write_frame;
    uint32_t buffered_frames;
    uint32_t target_buffer_ms;
    uint32_t reported_target_buffer_ms;
    uint32_t maximum_buffered_frames;
    uint16_t callback_frames;
    uint32_t last_sequence;
    uint64_t last_packet_receive_us;
    uint64_t maximum_arrival_gap_us;
    uint64_t last_target_adjustment_us;
    uint64_t packets_received;
    uint64_t packets_dropped;
    uint64_t packets_stale;
    uint64_t underflows;
    uint64_t reported_underflows;
    uint64_t trim_events;
    uint64_t frames_trimmed;
    uint64_t payload_bytes;
    uint64_t decode_time_us;
    uint64_t plc_frames;
    uint64_t decode_errors;
    double resample_phase;
    double rate_correction;
    uint8_t codec;
    bool has_sequence;
    bool playing;
    bool open_failed;
} RemoteAudioReceiver;

static uint64_t monotonic_time_us(void)
{
    uint64_t counter = SDL_GetPerformanceCounter();
    uint64_t frequency = SDL_GetPerformanceFrequency();
    return counter / frequency * 1000000 + counter % frequency * 1000000 / frequency;
}

static uint64_t client_elapsed_ms(void)
{
    uint64_t now = monotonic_time_us();
    return client_start_time_us && now >= client_start_time_us?
        (now - client_start_time_us) / 1000 : 0;
}

static bool send_input(RemoteUdpSocket *transport,
                       uint32_t session_id,
                       uint32_t sequence,
                       uint16_t buttons,
                       uint64_t event_timestamp_us)
{
    uint64_t send_timestamp_us = monotonic_time_us();
    RemotePlayInputPacket packet = {
        .session_id = session_id,
        .sequence = sequence,
        .buttons = buttons,
        .client_event_timestamp_us = event_timestamp_us,
        .client_send_timestamp_us = send_timestamp_us,
    };
    uint8_t encoded[REMOTE_PLAY_INPUT_PACKET_SIZE];
    remote_play_encode_input_packet(encoded, &packet);

    char error[128];
    if (!remote_udp_send(transport, encoded, sizeof(encoded), error, sizeof(error))) {
        fprintf(stderr, "[SameBoy Link][frontend] remote_input_client send_error=%s\n", error);
        return false;
    }
    return true;
}

static bool send_clock_sync(RemoteUdpSocket *transport,
                            uint32_t session_id,
                            RemoteClockSync *clock_sync)
{
    uint64_t send_timestamp_us = monotonic_time_us();
    RemotePlayClockSyncPing packet = {
        .session_id = session_id,
        .sequence = ++clock_sync->sequence,
        .client_send_timestamp_us = send_timestamp_us,
    };
    uint8_t encoded[REMOTE_PLAY_CLOCK_SYNC_PING_SIZE];
    remote_play_encode_clock_sync_ping(encoded, &packet);
    char error[128];
    if (!remote_udp_send(transport, encoded, sizeof(encoded), error, sizeof(error))) {
        fprintf(stderr, "[SameBoy Link][timing] remote_clock_sync_client send_error=%s\n", error);
        return false;
    }
    clock_sync->last_send_timestamp_us = send_timestamp_us;
    return true;
}

static int64_t timestamp_difference(uint64_t left, uint64_t right)
{
    return (int64_t)(left - right);
}

static void receive_clock_sync_pong(RemoteClockSync *clock_sync,
                                    const RemotePlayClockSyncPong *packet,
                                    uint64_t client_receive_timestamp_us)
{
    if (!packet->sequence ||
        remote_play_sequence_is_newer(packet->sequence, clock_sync->sequence) ||
        (clock_sync->responses &&
         !remote_play_sequence_is_newer(packet->sequence,
                                        clock_sync->last_response_sequence)) ||
        client_receive_timestamp_us < packet->client_send_timestamp_us ||
        packet->host_send_timestamp_us < packet->host_receive_timestamp_us) {
        return;
    }
    uint64_t round_trip_span = client_receive_timestamp_us -
                               packet->client_send_timestamp_us;
    uint64_t host_processing = packet->host_send_timestamp_us -
                               packet->host_receive_timestamp_us;
    if (host_processing > round_trip_span) {
        return;
    }

    uint64_t rtt_us = round_trip_span - host_processing;
    int64_t sample_offset = (timestamp_difference(packet->host_receive_timestamp_us,
                                                   packet->client_send_timestamp_us) +
                             timestamp_difference(packet->host_send_timestamp_us,
                                                   client_receive_timestamp_us)) / 2;
    if (!clock_sync->synchronized) {
        clock_sync->min_rtt_us = rtt_us;
        clock_sync->smoothed_rtt_us = rtt_us;
        clock_sync->host_minus_client_us = sample_offset;
        clock_sync->synchronized = true;
    }
    else {
        double difference = rtt_us - clock_sync->smoothed_rtt_us;
        if (difference < 0) difference = -difference;
        clock_sync->jitter_us += (difference - clock_sync->jitter_us) / 16.0;
        clock_sync->smoothed_rtt_us += (rtt_us - clock_sync->smoothed_rtt_us) / 8.0;
        if (rtt_us < clock_sync->min_rtt_us) {
            clock_sync->min_rtt_us = rtt_us;
            clock_sync->host_minus_client_us = sample_offset;
        }
        else if (rtt_us <= clock_sync->min_rtt_us + 250) {
            clock_sync->host_minus_client_us =
                (clock_sync->host_minus_client_us * 7 + sample_offset) / 8;
        }
    }
    clock_sync->last_response_sequence = packet->sequence;
    clock_sync->responses++;
    if (clock_sync->responses == 1 || clock_sync->responses % 10 == 0) {
        fprintf(stderr,
                "[SameBoy Link][timing] remote_clock_sync_client samples=%llu rtt_ms=%.3f jitter_ms=%.3f min_rtt_ms=%.3f offset_us=%lld uncertainty_ms=%.3f\n",
                (unsigned long long)clock_sync->responses,
                clock_sync->smoothed_rtt_us / 1000.0,
                clock_sync->jitter_us / 1000.0,
                clock_sync->min_rtt_us / 1000.0,
                (long long)clock_sync->host_minus_client_us,
                clock_sync->min_rtt_us / 2000.0);
    }
}

static void render_client(SDL_Renderer *renderer,
                          SDL_Texture *video_texture,
                          uint16_t buttons)
{
    SDL_SetRenderDrawColor(renderer, 15, 22, 30, 255);
    SDL_RenderClear(renderer);

    SDL_Rect video_destination = {0};
    if (video_texture) {
        int texture_width = 0;
        int texture_height = 0;
        int output_width = 0;
        int output_height = 0;
        SDL_QueryTexture(video_texture,
                         NULL,
                         NULL,
                         &texture_width,
                         &texture_height);
        SDL_GetRendererOutputSize(renderer, &output_width, &output_height);
        if (texture_width > 0 && texture_height > 0 &&
            output_width > 0 && output_height > 0) {
            if (configuration.scaling_mode == GB_SDL_SCALING_ENTIRE_WINDOW) {
                video_destination.w = output_width;
                video_destination.h = output_height;
            }
            else {
                double scale_x = (double)output_width / texture_width;
                double scale_y = (double)output_height / texture_height;
                double scale = scale_x < scale_y? scale_x : scale_y;
                if (configuration.scaling_mode == GB_SDL_SCALING_INTEGER_FACTOR &&
                    scale >= 1.0) {
                    scale = (unsigned)scale;
                }
                video_destination.w = (int)(texture_width * scale);
                video_destination.h = (int)(texture_height * scale);
            }
            video_destination.x = (output_width - video_destination.w) / 2;
            video_destination.y = (output_height - video_destination.h) / 2;
            SDL_SetTextureScaleMode(video_texture,
                                    configuration.remote_client_filter?
                                        SDL_ScaleModeLinear : SDL_ScaleModeNearest);
            SDL_RenderCopy(renderer, video_texture, NULL, &video_destination);
        }
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_Rect indicator = {
        video_texture? video_destination.x + 6 : 6,
        video_texture? video_destination.y + 6 : 6,
        8,
        8,
    };
    for (unsigned bit = 0; bit < 8; bit++) {
        if (buttons & (1u << bit)) {
            SDL_SetRenderDrawColor(renderer, 65, 210, 130, 230);
        }
        else {
            SDL_SetRenderDrawColor(renderer, 25, 35, 45, 180);
        }
        indicator.x = (video_texture? video_destination.x : 0) + 6 + bit * 11;
        SDL_RenderFillRect(renderer, &indicator);
    }
    SDL_RenderPresent(renderer);
}

static RemoteFrameTelemetry frame_telemetry_from_chunk(const RemotePlayVideoChunk *chunk,
                                                       uint64_t receive_timestamp_us)
{
    return (RemoteFrameTelemetry){
        .input_sequence = chunk->input_sequence,
        .client_input_event_timestamp_us = chunk->client_input_event_timestamp_us,
        .client_input_send_timestamp_us = chunk->client_input_send_timestamp_us,
        .host_input_receive_timestamp_us = chunk->host_input_receive_timestamp_us,
        .host_input_apply_timestamp_us = chunk->host_input_apply_timestamp_us,
        .host_frame_complete_timestamp_us = chunk->host_frame_complete_timestamp_us,
        .host_encode_begin_timestamp_us = chunk->host_encode_begin_timestamp_us,
        .host_encode_end_timestamp_us = chunk->host_encode_end_timestamp_us,
        .host_send_timestamp_us = chunk->host_send_timestamp_us,
        .client_first_receive_timestamp_us = receive_timestamp_us,
        .client_last_receive_timestamp_us = receive_timestamp_us,
    };
}

static bool chunk_telemetry_matches(const RemoteFrameTelemetry *telemetry,
                                    const RemotePlayVideoChunk *chunk)
{
    return telemetry->input_sequence == chunk->input_sequence &&
           telemetry->client_input_event_timestamp_us == chunk->client_input_event_timestamp_us &&
           telemetry->client_input_send_timestamp_us == chunk->client_input_send_timestamp_us &&
           telemetry->host_input_receive_timestamp_us == chunk->host_input_receive_timestamp_us &&
           telemetry->host_input_apply_timestamp_us == chunk->host_input_apply_timestamp_us &&
           telemetry->host_frame_complete_timestamp_us == chunk->host_frame_complete_timestamp_us &&
           telemetry->host_encode_begin_timestamp_us == chunk->host_encode_begin_timestamp_us &&
           telemetry->host_encode_end_timestamp_us == chunk->host_encode_end_timestamp_us &&
           telemetry->host_send_timestamp_us == chunk->host_send_timestamp_us;
}

static void begin_video_frame(RemoteVideoReceiver *receiver,
                              const RemotePlayVideoChunk *chunk,
                              uint64_t receive_timestamp_us)
{
    if (receiver->has_assembly && receiver->received_chunk_count) {
        receiver->frames_dropped++;
        fprintf(stderr,
                "[SameBoy Link][video] remote_video_client incomplete_frame_drop t_ms=%llu sequence=%llu received_chunks=%u expected_chunks=%u\n",
                (unsigned long long)client_elapsed_ms(),
                (unsigned long long)receiver->assembling_sequence,
                receiver->received_chunk_count,
                receiver->assembling_chunk_count);
    }
    memset(receiver->chunks_received, 0, sizeof(receiver->chunks_received));
    receiver->assembling_sequence = chunk->frame_sequence;
    receiver->assembling_width = chunk->width;
    receiver->assembling_height = chunk->height;
    receiver->assembling_chunk_count = chunk->chunk_count;
    receiver->assembling_frame_size = chunk->frame_size;
    receiver->assembling_pixel_format = chunk->pixel_format;
    receiver->assembling_telemetry = frame_telemetry_from_chunk(chunk,
                                                                receive_timestamp_us);
    receiver->received_chunk_count = 0;
    receiver->has_assembly = true;
}

static int16_t read_audio_s16le(const uint8_t *input)
{
    return (int16_t)((uint16_t)input[0] | (uint16_t)input[1] << 8);
}

static void remote_audio_callback(void *userdata, uint8_t *stream, int byte_count)
{
    RemoteAudioReceiver *receiver = userdata;
    memset(stream, 0, byte_count);
    if (!receiver->ring_samples || byte_count <= 0 || !receiver->sample_rate) {
        return;
    }

    uint32_t output_frames = (uint32_t)byte_count / REMOTE_PLAY_AUDIO_BYTES_PER_FRAME;
    uint32_t target_frames = receiver->sample_rate * receiver->target_buffer_ms / 1000;
    if (!receiver->playing) {
        if (receiver->buffered_frames < target_frames) {
            return;
        }
        receiver->playing = true;
        receiver->resample_phase = 0;
    }

    double error_seconds = ((double)receiver->buffered_frames - target_frames) /
                           receiver->sample_rate;
    receiver->rate_correction = error_seconds * REMOTE_AUDIO_DRIFT_GAIN;
    if (receiver->rate_correction > REMOTE_AUDIO_MAX_RATE_CORRECTION) {
        receiver->rate_correction = REMOTE_AUDIO_MAX_RATE_CORRECTION;
    }
    else if (receiver->rate_correction < -REMOTE_AUDIO_MAX_RATE_CORRECTION) {
        receiver->rate_correction = -REMOTE_AUDIO_MAX_RATE_CORRECTION;
    }
    double input_frames_per_output = 1.0 + receiver->rate_correction;
    int16_t *output = (int16_t *)stream;
    uint32_t frames_written = 0;
    while (frames_written < output_frames && receiver->buffered_frames >= 2) {
        uint32_t current = receiver->ring_read_frame;
        uint32_t next = (current + 1) % receiver->ring_capacity_frames;
        double fraction = receiver->resample_phase;
        for (unsigned channel = 0; channel < 2; channel++) {
            int16_t first = receiver->ring_samples[current * 2 + channel];
            int16_t second = receiver->ring_samples[next * 2 + channel];
            int sample = (int)(first + (second - first) * fraction);
            if (configuration.remote_client_muted) {
                sample = 0;
            }
            else {
                sample = sample * configuration.volume / 100;
            }
            output[frames_written * 2 + channel] = (int16_t)sample;
        }

        receiver->resample_phase += input_frames_per_output;
        uint32_t consumed = (uint32_t)receiver->resample_phase;
        if (consumed > receiver->buffered_frames - 1) {
            consumed = receiver->buffered_frames - 1;
        }
        receiver->ring_read_frame =
            (receiver->ring_read_frame + consumed) % receiver->ring_capacity_frames;
        receiver->buffered_frames -= consumed;
        receiver->resample_phase -= consumed;
        frames_written++;
    }

    if (frames_written < output_frames) {
        receiver->playing = false;
        receiver->resample_phase = 0;
        receiver->underflows++;
        if (receiver->target_buffer_ms < REMOTE_AUDIO_MAX_TARGET_MS) {
            uint32_t target = receiver->target_buffer_ms +
                              REMOTE_AUDIO_TARGET_STEP_MS * 2;
            receiver->target_buffer_ms = target < REMOTE_AUDIO_MAX_TARGET_MS?
                target : REMOTE_AUDIO_MAX_TARGET_MS;
        }
        receiver->last_target_adjustment_us = monotonic_time_us();
    }
}

static void write_audio_frames_locked(RemoteAudioReceiver *receiver,
                                      const int16_t *samples,
                                      uint32_t frame_count)
{
    if (frame_count > receiver->ring_capacity_frames) {
        uint32_t skipped = frame_count - receiver->ring_capacity_frames;
        frame_count = receiver->ring_capacity_frames;
        if (samples) {
            samples += skipped * 2;
        }
    }

    uint32_t overflow = receiver->buffered_frames + frame_count >
                        receiver->ring_capacity_frames?
        receiver->buffered_frames + frame_count - receiver->ring_capacity_frames : 0;
    if (overflow) {
        receiver->ring_read_frame =
            (receiver->ring_read_frame + overflow) % receiver->ring_capacity_frames;
        receiver->buffered_frames -= overflow;
        receiver->resample_phase = 0;
        receiver->trim_events++;
        receiver->frames_trimmed += overflow;
    }

    for (uint32_t frame = 0; frame < frame_count; frame++) {
        int16_t left = 0;
        int16_t right = 0;
        if (samples) {
            left = samples[frame * 2];
            right = samples[frame * 2 + 1];
        }
        receiver->ring_samples[receiver->ring_write_frame * 2] = left;
        receiver->ring_samples[receiver->ring_write_frame * 2 + 1] = right;
        receiver->ring_write_frame =
            (receiver->ring_write_frame + 1) % receiver->ring_capacity_frames;
    }
    receiver->buffered_frames += frame_count;
    if (receiver->buffered_frames > receiver->maximum_buffered_frames) {
        receiver->maximum_buffered_frames = receiver->buffered_frames;
    }
}

static bool open_audio_device(RemoteAudioReceiver *receiver, uint32_t sample_rate)
{
    if (receiver->device && receiver->sample_rate == sample_rate) {
        return true;
    }
    if (receiver->device) {
        SDL_CloseAudioDevice(receiver->device);
        receiver->device = 0;
    }
    SDL_free(receiver->ring_samples);
    receiver->ring_samples = NULL;

    receiver->ring_capacity_frames = sample_rate * REMOTE_AUDIO_MAX_QUEUE_MS / 1000;
    receiver->ring_samples = SDL_calloc(receiver->ring_capacity_frames * 2,
                                        sizeof(*receiver->ring_samples));
    if (!receiver->ring_samples) {
        if (!receiver->open_failed) {
            fprintf(stderr,
                    "[SameBoy Link][audio] remote_audio_client open_error=out of memory\n");
        }
        receiver->open_failed = true;
        return false;
    }
    receiver->ring_read_frame = 0;
    receiver->ring_write_frame = 0;
    receiver->buffered_frames = 0;
    receiver->resample_phase = 0;
    receiver->rate_correction = 0;
    receiver->target_buffer_ms = REMOTE_AUDIO_INITIAL_TARGET_MS;
    receiver->reported_target_buffer_ms = receiver->target_buffer_ms;
    receiver->last_target_adjustment_us = monotonic_time_us();

    SDL_AudioSpec wanted = {
        .freq = (int)sample_rate,
        .format = AUDIO_S16LSB,
        .channels = 2,
        .samples = 256,
        .callback = remote_audio_callback,
        .userdata = receiver,
    };
    SDL_AudioSpec obtained = {0};
    receiver->device = SDL_OpenAudioDevice(NULL,
                                            0,
                                            &wanted,
                                            &obtained,
                                            0);
    receiver->sample_rate = sample_rate;
    receiver->playing = false;
    if (!receiver->device || obtained.freq != (int)sample_rate ||
        obtained.format != AUDIO_S16LSB || obtained.channels != 2) {
        if (receiver->device) {
            SDL_CloseAudioDevice(receiver->device);
            receiver->device = 0;
        }
        SDL_free(receiver->ring_samples);
        receiver->ring_samples = NULL;
        if (!receiver->open_failed) {
            fprintf(stderr,
                    "[SameBoy Link][audio] remote_audio_client open_error=%s\n",
                    SDL_GetError());
        }
        receiver->open_failed = true;
        return false;
    }

    receiver->open_failed = false;
    receiver->callback_frames = obtained.samples;
    SDL_PauseAudioDevice(receiver->device, 0);
    fprintf(stderr,
            "[SameBoy Link][audio] remote_audio_client device_open rate=%u format=S16LE_stereo callback_frames=%u callback_ms=%.3f jitter_buffer=adaptive target_ms=%u target_range_ms=%u-%u max_buffer_ms=%u\n",
            sample_rate,
            receiver->callback_frames,
            receiver->callback_frames * 1000.0 / sample_rate,
            receiver->target_buffer_ms,
            REMOTE_AUDIO_MIN_TARGET_MS,
            REMOTE_AUDIO_MAX_TARGET_MS,
            REMOTE_AUDIO_MAX_QUEUE_MS);
    return true;
}

static const char *remote_audio_codec_name(uint8_t codec)
{
    return codec == REMOTE_PLAY_AUDIO_CODEC_OPUS? "opus" : "pcm_s16le";
}

static bool prepare_audio_decoder(RemoteAudioReceiver *receiver, uint8_t codec)
{
    if (receiver->codec == codec &&
        (codec != REMOTE_PLAY_AUDIO_CODEC_OPUS || receiver->decoder)) {
        return true;
    }
#ifdef ENABLE_REMOTE_OPUS
    if (receiver->decoder) {
        opus_decoder_destroy(receiver->decoder);
        receiver->decoder = NULL;
    }
#endif
    receiver->codec = codec;
    if (codec != REMOTE_PLAY_AUDIO_CODEC_OPUS) {
        return true;
    }
#ifdef ENABLE_REMOTE_OPUS
    int error = OPUS_OK;
    OpusDecoder *decoder = opus_decoder_create(48000, 2, &error);
    if (!decoder || error != OPUS_OK) {
        fprintf(stderr,
                "[SameBoy Link][audio] remote_audio_client opus_create_error=%s\n",
                opus_strerror(error));
        if (decoder) opus_decoder_destroy(decoder);
        receiver->decode_errors++;
        return false;
    }
    receiver->decoder = decoder;
    return true;
#else
    if (!receiver->decode_errors) {
        fprintf(stderr,
                "[SameBoy Link][audio] remote_audio_client opus_error=build has no Opus support\n");
    }
    receiver->decode_errors++;
    return false;
#endif
}

static void receive_audio_packet(RemoteAudioReceiver *receiver,
                                 const RemotePlayAudioPacket *packet)
{
    uint32_t missing_packets = 0;
    if (receiver->has_sequence) {
        if (!remote_play_sequence_is_newer(packet->sequence, receiver->last_sequence)) {
            receiver->packets_stale++;
            return;
        }
        missing_packets = packet->sequence - receiver->last_sequence - 1;
        receiver->packets_dropped += missing_packets;
        if (missing_packets) {
            fprintf(stderr,
                    "[SameBoy Link][audio] remote_audio_client sequence_gap t_ms=%llu missing_packets=%u previous=%u current=%u\n",
                    (unsigned long long)client_elapsed_ms(),
                    missing_packets,
                    receiver->last_sequence,
                    packet->sequence);
        }
    }
    receiver->last_sequence = packet->sequence;
    receiver->has_sequence = true;
    if (!open_audio_device(receiver, packet->sample_rate)) {
        return;
    }
    if (!prepare_audio_decoder(receiver, packet->codec)) {
        return;
    }

    uint64_t receive_time_us = monotonic_time_us();
    uint64_t arrival_gap_us = receiver->last_packet_receive_us?
        receive_time_us - receiver->last_packet_receive_us : 0;
    receiver->last_packet_receive_us = receive_time_us;
    if (arrival_gap_us > receiver->maximum_arrival_gap_us) {
        receiver->maximum_arrival_gap_us = arrival_gap_us;
    }

    SDL_LockAudioDevice(receiver->device);
    bool target_raised = false;
    if (arrival_gap_us && arrival_gap_us < 500000) {
        uint32_t protective_target_ms =
            (uint32_t)((arrival_gap_us + 999) / 1000) + 10;
        protective_target_ms =
            (protective_target_ms + REMOTE_AUDIO_TARGET_STEP_MS - 1) /
            REMOTE_AUDIO_TARGET_STEP_MS * REMOTE_AUDIO_TARGET_STEP_MS;
        if (protective_target_ms > receiver->target_buffer_ms) {
            receiver->target_buffer_ms = protective_target_ms < REMOTE_AUDIO_MAX_TARGET_MS?
                protective_target_ms : REMOTE_AUDIO_MAX_TARGET_MS;
            receiver->last_target_adjustment_us = receive_time_us;
            target_raised = true;
        }
    }
    if (!target_raised &&
        receiver->target_buffer_ms > REMOTE_AUDIO_MIN_TARGET_MS &&
        receive_time_us - receiver->last_target_adjustment_us >=
            REMOTE_AUDIO_TARGET_DECAY_US) {
        receiver->target_buffer_ms -= REMOTE_AUDIO_TARGET_STEP_MS;
        receiver->last_target_adjustment_us = receive_time_us;
    }
    uint32_t missing_packets_to_buffer = missing_packets;
    uint32_t maximum_missing_packets = receiver->ring_capacity_frames /
                                       packet->frame_count;
    if (missing_packets_to_buffer > maximum_missing_packets) {
        missing_packets_to_buffer = maximum_missing_packets;
    }
    uint64_t decode_begin_us = monotonic_time_us();
    int16_t decoded[REMOTE_PLAY_AUDIO_FRAMES_PER_PACKET * 2];
    for (uint32_t missing = 0; missing < missing_packets_to_buffer; missing++) {
        if (packet->codec == REMOTE_PLAY_AUDIO_CODEC_OPUS) {
#ifdef ENABLE_REMOTE_OPUS
            int result = opus_decode(receiver->decoder,
                                     NULL,
                                     0,
                                     decoded,
                                     packet->frame_count,
                                     0);
            if (result == packet->frame_count) {
                write_audio_frames_locked(receiver, decoded, (uint32_t)result);
                receiver->plc_frames += result;
            }
            else {
                receiver->decode_errors++;
                write_audio_frames_locked(receiver, NULL, packet->frame_count);
            }
#endif
        }
        else {
            write_audio_frames_locked(receiver, NULL, packet->frame_count);
        }
    }

    uint32_t decoded_frames = packet->frame_count;
    if (packet->codec == REMOTE_PLAY_AUDIO_CODEC_OPUS) {
#ifdef ENABLE_REMOTE_OPUS
        int result = opus_decode(receiver->decoder,
                                 packet->payload,
                                 packet->payload_size,
                                 decoded,
                                 packet->frame_count,
                                 0);
        if (result != packet->frame_count) {
            receiver->decode_errors++;
            decoded_frames = 0;
        }
#endif
    }
    else {
        for (uint32_t sample = 0; sample < packet->frame_count * 2; sample++) {
            decoded[sample] = read_audio_s16le(packet->payload +
                                               sample * sizeof(int16_t));
        }
    }
    if (decoded_frames) {
        write_audio_frames_locked(receiver, decoded, decoded_frames);
    }
    receiver->decode_time_us += monotonic_time_us() - decode_begin_us;
    uint32_t queued_frames = receiver->buffered_frames;
    uint32_t target_buffer_ms = receiver->target_buffer_ms;
    uint64_t underflows = receiver->underflows;
    uint64_t trim_events = receiver->trim_events;
    double rate_correction = receiver->rate_correction;
    SDL_UnlockAudioDevice(receiver->device);

    if (underflows != receiver->reported_underflows) {
        receiver->reported_underflows = underflows;
        fprintf(stderr,
                "[SameBoy Link][audio] remote_audio_client underflow t_ms=%llu count=%llu new_target_ms=%u\n",
                (unsigned long long)client_elapsed_ms(),
                (unsigned long long)underflows,
                target_buffer_ms);
    }
    if (target_buffer_ms != receiver->reported_target_buffer_ms) {
        fprintf(stderr,
                "[SameBoy Link][audio] remote_audio_client target_changed t_ms=%llu previous_ms=%u current_ms=%u arrival_gap_ms=%.3f\n",
                (unsigned long long)client_elapsed_ms(),
                receiver->reported_target_buffer_ms,
                target_buffer_ms,
                arrival_gap_us / 1000.0);
        receiver->reported_target_buffer_ms = target_buffer_ms;
    }
    receiver->packets_received++;
    receiver->payload_bytes += packet->payload_size;
    if (receiver->packets_received == 1) {
        fprintf(stderr,
                "[SameBoy Link][audio] remote_audio_client first_packet sequence=%u rate=%u frames=%u codec=%s payload_bytes=%u target_ms=%u\n",
                packet->sequence,
                packet->sample_rate,
                packet->frame_count,
                remote_audio_codec_name(packet->codec),
                packet->payload_size,
                target_buffer_ms);
    }
    else if (receiver->packets_received % 600 == 0) {
        fprintf(stderr,
                "[SameBoy Link][audio] remote_audio_client packets=%llu dropped=%llu stale=%llu underflows=%llu trims=%llu queued_ms=%u target_ms=%u rate_ppm=%.0f max_arrival_gap_ms=%.3f codec=%s average_payload_bytes=%.1f average_decode_us=%.2f plc_frames=%llu decode_errors=%llu\n",
                (unsigned long long)receiver->packets_received,
                (unsigned long long)receiver->packets_dropped,
                (unsigned long long)receiver->packets_stale,
                (unsigned long long)receiver->underflows,
                (unsigned long long)trim_events,
                queued_frames * 1000 / packet->sample_rate,
                target_buffer_ms,
                rate_correction * 1000000.0,
                receiver->maximum_arrival_gap_us / 1000.0,
                remote_audio_codec_name(receiver->codec),
                (double)receiver->payload_bytes / receiver->packets_received,
                (double)receiver->decode_time_us / receiver->packets_received,
                (unsigned long long)receiver->plc_frames,
                (unsigned long long)receiver->decode_errors);
    }
}

static void receive_remote_packets(RemoteUdpSocket *transport,
                                   uint32_t session_id,
                                   RemoteVideoReceiver *video,
                                   RemoteAudioReceiver *audio,
                                   RemoteClockSync *clock_sync)
{
    uint8_t encoded[REMOTE_PLAY_VIDEO_PACKET_MAX_SIZE];
    char error[128];
    for (unsigned processed = 0; processed < 256; processed++) {
        int size = remote_udp_receive(transport,
                                      encoded,
                                      sizeof(encoded),
                                      NULL,
                                      error,
                                      sizeof(error));
        if (size == 0) {
            return;
        }
        if (size < 0) {
            fprintf(stderr, "[SameBoy Link][video] remote_video_client receive_error=%s\n", error);
            return;
        }
        uint64_t receive_timestamp_us = monotonic_time_us();

        RemotePlayClockSyncPong pong;
        if (remote_play_decode_clock_sync_pong(&pong, encoded, size) &&
            pong.session_id == session_id) {
            receive_clock_sync_pong(clock_sync, &pong, receive_timestamp_us);
            continue;
        }

        RemotePlayAudioPacket audio_packet;
        if (remote_play_decode_audio_packet(&audio_packet, encoded, size) &&
            audio_packet.session_id == session_id) {
            receive_audio_packet(audio, &audio_packet);
            continue;
        }

        RemotePlayVideoChunk chunk;
        if (!remote_play_decode_video_chunk(&chunk, encoded, size) ||
            chunk.session_id != session_id) {
            video->packets_rejected++;
            continue;
        }
        if (chunk.frame_sequence <= video->completed_sequence ||
            (video->has_assembly && chunk.frame_sequence < video->assembling_sequence)) {
            continue;
        }
        if (chunk.frame_sequence > video->highest_seen_sequence) {
            if (video->highest_seen_sequence &&
                chunk.frame_sequence > video->highest_seen_sequence + 1) {
                uint64_t gap = chunk.frame_sequence - video->highest_seen_sequence - 1;
                video->frames_dropped += gap;
                fprintf(stderr,
                        "[SameBoy Link][video] remote_video_client sequence_gap t_ms=%llu missing_frames=%llu previous=%llu current=%llu\n",
                        (unsigned long long)client_elapsed_ms(),
                        (unsigned long long)gap,
                        (unsigned long long)video->highest_seen_sequence,
                        (unsigned long long)chunk.frame_sequence);
            }
            video->highest_seen_sequence = chunk.frame_sequence;
        }
        if (!video->has_assembly || chunk.frame_sequence > video->assembling_sequence) {
            begin_video_frame(video, &chunk, receive_timestamp_us);
        }
        if (chunk.width != video->assembling_width ||
            chunk.height != video->assembling_height ||
            chunk.chunk_count != video->assembling_chunk_count ||
            chunk.frame_size != video->assembling_frame_size ||
            chunk.pixel_format != video->assembling_pixel_format ||
            !chunk_telemetry_matches(&video->assembling_telemetry, &chunk) ||
            video->chunks_received[chunk.chunk_index]) {
            continue;
        }

        memcpy(video->frame_buffers[video->assembly_buffer] + chunk.payload_offset,
               chunk.payload,
               chunk.payload_size);
        video->chunks_received[chunk.chunk_index] = true;
        video->received_chunk_count++;
        video->assembling_telemetry.client_last_receive_timestamp_us = receive_timestamp_us;
        if (video->received_chunk_count == video->assembling_chunk_count) {
            video->assembling_telemetry.client_decode_begin_timestamp_us = monotonic_time_us();
            unsigned completed_buffer = video->assembly_buffer;
            if (video->assembling_pixel_format == REMOTE_PLAY_VIDEO_FORMAT_RLE_RGBA8) {
                completed_buffer = !video->assembly_buffer;
                size_t decoded_size = (size_t)video->assembling_width *
                                      video->assembling_height * 4;
                if (!remote_video_rle_decode_rgba8(video->frame_buffers[completed_buffer],
                                                   decoded_size,
                                                   video->frame_buffers[video->assembly_buffer],
                                                   video->assembling_frame_size)) {
                    video->packets_rejected++;
                    video->frames_dropped++;
                    video->has_assembly = false;
                    continue;
                }
            }
            video->assembling_telemetry.client_decode_end_timestamp_us = monotonic_time_us();
            video->completed_sequence = video->assembling_sequence;
            video->completed_width = video->assembling_width;
            video->completed_height = video->assembling_height;
            video->completed_buffer = completed_buffer;
            video->completed_telemetry = video->assembling_telemetry;
            if (video->assembling_pixel_format == REMOTE_PLAY_VIDEO_FORMAT_RGBA8) {
                video->assembly_buffer = !video->assembly_buffer;
            }
            video->has_assembly = false;
            if (video->frame_ready) {
                video->frames_superseded_before_present++;
            }
            video->frame_ready = true;
            video->frames_completed++;
            video->raw_bytes_completed +=
                (uint64_t)video->completed_width * video->completed_height * 4;
            video->encoded_bytes_completed += video->assembling_frame_size;
            if (video->frames_completed == 1) {
                fprintf(stderr,
                        "[SameBoy Link][video] remote_video_client first_frame=%llu size=%ux%u encoded_bytes=%u chunks=%u format=%s\n",
                        (unsigned long long)video->completed_sequence,
                        video->completed_width,
                        video->completed_height,
                        video->assembling_frame_size,
                        video->assembling_chunk_count,
                        video->assembling_pixel_format == REMOTE_PLAY_VIDEO_FORMAT_RLE_RGBA8?
                            "RLE_RGBA8" : "RGBA8");
            }
            else if (video->frames_completed % 120 == 0) {
                fprintf(stderr,
                        "[SameBoy Link][video] remote_video_client frames=%llu dropped=%llu rejected=%llu latest=%llu encoded_ratio=%.3f\n",
                        (unsigned long long)video->frames_completed,
                        (unsigned long long)video->frames_dropped,
                        (unsigned long long)video->packets_rejected,
                        (unsigned long long)video->completed_sequence,
                        video->raw_bytes_completed?
                            (double)video->encoded_bytes_completed / video->raw_bytes_completed : 0.0);
            }
        }
    }
}

static double duration_ms(uint64_t end, uint64_t begin)
{
    return end >= begin? (end - begin) / 1000.0 : -1.0;
}

static double host_to_client_time_us(const RemoteClockSync *clock_sync,
                                     uint64_t host_timestamp_us)
{
    return (double)host_timestamp_us - clock_sync->host_minus_client_us;
}

static void record_presented_frame_latency(RemoteLatencyStats *latency,
                                           const RemoteClockSync *clock_sync,
                                           const RemoteFrameTelemetry *frame,
                                           uint64_t upload_begin_us,
                                           uint64_t upload_end_us,
                                           uint64_t present_begin_us,
                                           uint64_t present_end_us)
{
    if (!frame->input_sequence ||
        frame->input_sequence == latency->last_input_sequence ||
        !frame->client_input_event_timestamp_us) {
        return;
    }
    latency->last_input_sequence = frame->input_sequence;

    double total_ms = duration_ms(present_end_us,
                                  frame->client_input_event_timestamp_us);
    if (total_ms < 0 || total_ms > 5000) {
        return;
    }
    latency->samples++;
    latency->latest_total_ms = total_ms;
    latency->average_total_ms +=
        (total_ms - latency->average_total_ms) / latency->samples;
    if (total_ms > latency->maximum_total_ms) {
        latency->maximum_total_ms = total_ms;
    }

    double input_network_ms = -1.0;
    double video_network_ms = -1.0;
    if (clock_sync->synchronized) {
        input_network_ms =
            (host_to_client_time_us(clock_sync, frame->host_input_receive_timestamp_us) -
             frame->client_input_send_timestamp_us) / 1000.0;
        video_network_ms =
            (frame->client_last_receive_timestamp_us -
             host_to_client_time_us(clock_sync, frame->host_send_timestamp_us)) / 1000.0;
    }

    fprintf(stderr,
            "[SameBoy Link][timing] remote_latency input_sequence=%u total_ms=%.3f event_to_send_ms=%.3f input_network_ms=%.3f host_receive_to_apply_ms=%.3f host_apply_to_frame_ms=%.3f frame_to_encode_ms=%.3f encode_ms=%.3f video_network_ms=%.3f receive_span_ms=%.3f decode_ms=%.3f upload_ms=%.3f present_call_ms=%.3f rtt_ms=%.3f clock_uncertainty_ms=%.3f\n",
            frame->input_sequence,
            total_ms,
            duration_ms(frame->client_input_send_timestamp_us,
                        frame->client_input_event_timestamp_us),
            input_network_ms,
            duration_ms(frame->host_input_apply_timestamp_us,
                        frame->host_input_receive_timestamp_us),
            duration_ms(frame->host_frame_complete_timestamp_us,
                        frame->host_input_apply_timestamp_us),
            duration_ms(frame->host_encode_begin_timestamp_us,
                        frame->host_frame_complete_timestamp_us),
            duration_ms(frame->host_encode_end_timestamp_us,
                        frame->host_encode_begin_timestamp_us),
            video_network_ms,
            duration_ms(frame->client_last_receive_timestamp_us,
                        frame->client_first_receive_timestamp_us),
            duration_ms(frame->client_decode_end_timestamp_us,
                        frame->client_decode_begin_timestamp_us),
            duration_ms(upload_end_us, upload_begin_us),
            duration_ms(present_end_us, present_begin_us),
            clock_sync->synchronized? clock_sync->smoothed_rtt_us / 1000.0 : -1.0,
            clock_sync->synchronized? clock_sync->min_rtt_us / 2000.0 : -1.0);
}

static uint32_t remote_audio_buffered_ms(RemoteAudioReceiver *audio,
                                         uint32_t *target_buffer_ms)
{
    if (!audio->device || !audio->sample_rate) {
        if (target_buffer_ms) *target_buffer_ms = 0;
        return 0;
    }
    SDL_LockAudioDevice(audio->device);
    uint32_t frames = audio->buffered_frames;
    if (target_buffer_ms) *target_buffer_ms = audio->target_buffer_ms;
    SDL_UnlockAudioDevice(audio->device);
    return frames * 1000 / audio->sample_rate;
}

static void update_latency_title(SDL_Window *window,
                                 const RemoteClockSync *clock_sync,
                                 const RemoteLatencyStats *latency,
                                 uint64_t video_frames_dropped,
                                 RemoteAudioReceiver *audio)
{
    char title[512];
    uint32_t audio_target_ms = 0;
    uint32_t audio_queue_ms = remote_audio_buffered_ms(audio, &audio_target_ms);
    snprintf(title,
             sizeof(title),
             "SameBoy Link P2 | RTT %.1f ms | +/-%.1f ms | Input %.1f ms | Jitter %.1f ms | VDrop %llu | AQ %u/%u ms",
             clock_sync->synchronized? clock_sync->smoothed_rtt_us / 1000.0 : 0.0,
             clock_sync->synchronized? clock_sync->min_rtt_us / 2000.0 : 0.0,
             latency->samples? latency->latest_total_ms : 0.0,
             clock_sync->synchronized? clock_sync->jitter_us / 1000.0 : 0.0,
             (unsigned long long)video_frames_dropped,
             audio_queue_ms,
             audio_target_ms);
    SDL_SetWindowTitle(window, title);
}

typedef struct {
    RemoteUdpSocket *transport;
    RemoteVideoReceiver *video;
    RemoteAudioReceiver *audio;
    RemoteClockSync *clock_sync;
    SDL_mutex *mutex;
    uint32_t session_id;
    uint32_t sequence;
    uint16_t buttons;
    uint64_t input_event_timestamp_us;
    uint64_t last_input_send_timestamp_us;
    bool input_changed;
    bool running;
} RemoteClientNetworkThread;

static int remote_client_network_thread(void *userdata)
{
    RemoteClientNetworkThread *network = userdata;
    SDL_SetThreadPriority(SDL_THREAD_PRIORITY_HIGH);
    fprintf(stderr,
            "[SameBoy Link][frontend] remote_network_thread started priority=high\n");

    for (;;) {
        SDL_LockMutex(network->mutex);
        if (!network->running) {
            send_input(network->transport,
                       network->session_id,
                       ++network->sequence,
                       0,
                       monotonic_time_us());
            SDL_UnlockMutex(network->mutex);
            break;
        }

        uint64_t now = monotonic_time_us();
        if (!network->clock_sync->last_send_timestamp_us ||
            now - network->clock_sync->last_send_timestamp_us >=
                CLOCK_SYNC_INTERVAL_US) {
            send_clock_sync(network->transport,
                            network->session_id,
                            network->clock_sync);
        }
        if (network->input_changed ||
            now - network->last_input_send_timestamp_us >= INPUT_KEEPALIVE_US) {
            send_input(network->transport,
                       network->session_id,
                       ++network->sequence,
                       network->buttons,
                       network->input_event_timestamp_us);
            network->last_input_send_timestamp_us = now;
            network->input_changed = false;
        }
        receive_remote_packets(network->transport,
                               network->session_id,
                               network->video,
                               network->audio,
                               network->clock_sync);
        SDL_UnlockMutex(network->mutex);
        SDL_Delay(1);
    }

    fprintf(stderr,
            "[SameBoy Link][frontend] remote_network_thread stopped packets_sent=%u\n",
            network->sequence);
    return 0;
}

int remote_play_client_run(const char *endpoint, uint32_t session_id)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS | SDL_INIT_TIMER) < 0) {
        fprintf(stderr, "Could not initialize SDL remote input client: %s\n", SDL_GetError());
        return 1;
    }
    client_start_time_us = monotonic_time_us();
    connect_joypad();

    RemoteUdpSocket transport;
    char error[128];
    if (!remote_udp_open_client(&transport, endpoint, error, sizeof(error))) {
        fprintf(stderr, "Could not start remote input client: %s\n", error);
        SDL_Quit();
        return 1;
    }

    char title[512];
    snprintf(title, sizeof(title),
             "SameBoy Link Remote P2 - %s - Configured P2 controls",
             endpoint);
    SDL_Window *client_window = SDL_CreateWindow(title,
                                                  SDL_WINDOWPOS_CENTERED,
                                                  SDL_WINDOWPOS_CENTERED,
                                                  480,
                                                  432,
                                                  SDL_WINDOW_ALLOW_HIGHDPI |
                                                  SDL_WINDOW_RESIZABLE);
    SDL_Renderer *client_renderer = NULL;
    if (client_window) {
        client_renderer = SDL_CreateRenderer(client_window, -1, SDL_RENDERER_PRESENTVSYNC);
    }
    if (!client_window || !client_renderer) {
        fprintf(stderr, "Could not create remote input window: %s\n", SDL_GetError());
        if (client_renderer) SDL_DestroyRenderer(client_renderer);
        if (client_window) SDL_DestroyWindow(client_window);
        remote_udp_close(&transport);
        SDL_Quit();
        return 1;
    }
    window = client_window;
    renderer = client_renderer;
    texture = SDL_CreateTexture(client_renderer,
                                SDL_PIXELFORMAT_ABGR8888,
                                SDL_TEXTUREACCESS_STREAMING,
                                160,
                                144);
    pixel_format = SDL_AllocFormat(SDL_PIXELFORMAT_ABGR8888);
    if (!texture || !pixel_format) {
        fprintf(stderr, "Could not create remote client menu resources: %s\n", SDL_GetError());
        if (texture) SDL_DestroyTexture(texture);
        if (pixel_format) SDL_FreeFormat(pixel_format);
        texture = NULL;
        pixel_format = NULL;
        renderer = NULL;
        window = NULL;
        SDL_DestroyRenderer(client_renderer);
        SDL_DestroyWindow(client_window);
        remote_udp_close(&transport);
        SDL_Quit();
        return 1;
    }
    SDL_SetWindowMinimumSize(client_window, 160, 144);
    if (configuration.remote_client_fullscreen) {
        SDL_SetWindowFullscreen(client_window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    }
    SDL_Texture *video_texture = NULL;
    unsigned texture_width = 0;
    unsigned texture_height = 0;

    fprintf(stderr,
            "[SameBoy Link][frontend] remote_input_client target=%s session=%u protocol=%u\n",
            endpoint,
            session_id,
            REMOTE_PLAY_PROTOCOL_VERSION);

    bool running = true;
    bool disconnected_to_frontend = false;
    bool render_needed = false;
    uint16_t buttons = 0;
    uint64_t last_input_event_time = monotonic_time_us();
    uint64_t last_title_update_time = 0;
    RemoteVideoReceiver video = {0};
    RemoteAudioReceiver audio = {0};
    RemoteClockSync clock_sync = {0};
    RemoteLatencyStats latency = {0};
    open_audio_device(&audio, 48000);
    render_client(client_renderer, video_texture, buttons);

    SDL_mutex *network_mutex = SDL_CreateMutex();
    RemoteClientNetworkThread network = {
        .transport = &transport,
        .video = &video,
        .audio = &audio,
        .clock_sync = &clock_sync,
        .mutex = network_mutex,
        .session_id = session_id,
        .buttons = buttons,
        .input_event_timestamp_us = last_input_event_time,
        .input_changed = true,
        .running = true,
    };
    SDL_Thread *network_thread = network_mutex?
        SDL_CreateThread(remote_client_network_thread,
                         "SameBoy Link Network",
                         &network) : NULL;
    if (!network_mutex || !network_thread) {
        fprintf(stderr,
                "Could not create remote network thread: %s\n",
                SDL_GetError());
        if (network_mutex) SDL_DestroyMutex(network_mutex);
        if (audio.device) SDL_CloseAudioDevice(audio.device);
#ifdef ENABLE_REMOTE_OPUS
        if (audio.decoder) opus_decoder_destroy(audio.decoder);
#endif
        SDL_free(audio.ring_samples);
        SDL_DestroyTexture(texture);
        SDL_FreeFormat(pixel_format);
        texture = NULL;
        pixel_format = NULL;
        renderer = NULL;
        window = NULL;
        SDL_DestroyRenderer(client_renderer);
        SDL_DestroyWindow(client_window);
        remote_udp_close(&transport);
        SDL_Quit();
        return 1;
    }

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
                break;
            }
            if (event.type == SDL_KEYDOWN &&
                event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                if (buttons) {
                    buttons = 0;
                    last_input_event_time = monotonic_time_us();
                    SDL_LockMutex(network_mutex);
                    network.buttons = buttons;
                    network.input_event_timestamp_us = last_input_event_time;
                    network.input_changed = true;
                    SDL_UnlockMutex(network_mutex);
                }
                SDL_RenderSetViewport(client_renderer, NULL);
                enum pending_command menu_command = run_remote_client_gui();
                SDL_RenderSetViewport(client_renderer, NULL);
                SDL_ShowCursor(SDL_DISABLE);
                if (menu_command == GB_SDL_DISCONNECT_LINK_COMMAND) {
                    disconnected_to_frontend = true;
                    running = false;
                    break;
                }
                if (menu_command == GB_SDL_QUIT_COMMAND) {
                    running = false;
                    break;
                }
                render_needed = true;
                continue;
            }
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                if (buttons) {
                    buttons = 0;
                    last_input_event_time = monotonic_time_us();
                    SDL_LockMutex(network_mutex);
                    network.buttons = buttons;
                    network.input_event_timestamp_us = last_input_event_time;
                    network.input_changed = true;
                    SDL_UnlockMutex(network_mutex);
                    render_needed = true;
                }
                continue;
            }
            if (event.type == SDL_WINDOWEVENT &&
                (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                 event.window.event == SDL_WINDOWEVENT_EXPOSED)) {
                render_needed = true;
                continue;
            }
            if (event.type == SDL_JOYDEVICEADDED ||
                event.type == SDL_JOYDEVICEREMOVED) {
                connect_joypad();
                continue;
            }

            uint16_t previous_buttons = buttons;
            bool input_handled = false;
            if ((event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) &&
                !event.key.repeat) {
                uint16_t button;
                if (multiplayer_input_button_for_remote_scancode(
                        event.key.keysym.scancode,
                        &button)) {
                    set_button_state(&buttons, button, event.type == SDL_KEYDOWN);
                    input_handled = true;
                }
            }
            else if (event.type == SDL_JOYBUTTONDOWN ||
                     event.type == SDL_JOYBUTTONUP) {
                if (joypad_player_for_instance(event.jbutton.which) >= 0) {
                    joypad_button_t button = get_player_joypad_button(
                        1,
                        event.jbutton.button);
                    if ((GB_key_t)button < GB_KEY_MAX) {
                        set_button_state(&buttons,
                                         (uint16_t)(1u << button),
                                         event.type == SDL_JOYBUTTONDOWN);
                        input_handled = true;
                    }
                }
            }
            else if (event.type == SDL_JOYAXISMOTION &&
                     joypad_player_for_instance(event.jaxis.which) >= 0) {
                joypad_axis_t axis = get_player_joypad_axis(1, event.jaxis.axis);
                if (axis == JOYPAD_AXISES_X) {
                    if (event.jaxis.value > JOYSTICK_HIGH) {
                        set_button_state(&buttons, MULTIPLAYER_BUTTON_RIGHT, true);
                        set_button_state(&buttons, MULTIPLAYER_BUTTON_LEFT, false);
                    }
                    else if (event.jaxis.value < -JOYSTICK_HIGH) {
                        set_button_state(&buttons, MULTIPLAYER_BUTTON_RIGHT, false);
                        set_button_state(&buttons, MULTIPLAYER_BUTTON_LEFT, true);
                    }
                    else if (event.jaxis.value < JOYSTICK_LOW &&
                             event.jaxis.value > -JOYSTICK_LOW) {
                        set_button_state(&buttons, MULTIPLAYER_BUTTON_RIGHT, false);
                        set_button_state(&buttons, MULTIPLAYER_BUTTON_LEFT, false);
                    }
                    input_handled = true;
                }
                else if (axis == JOYPAD_AXISES_Y) {
                    if (event.jaxis.value > JOYSTICK_HIGH) {
                        set_button_state(&buttons, MULTIPLAYER_BUTTON_DOWN, true);
                        set_button_state(&buttons, MULTIPLAYER_BUTTON_UP, false);
                    }
                    else if (event.jaxis.value < -JOYSTICK_HIGH) {
                        set_button_state(&buttons, MULTIPLAYER_BUTTON_DOWN, false);
                        set_button_state(&buttons, MULTIPLAYER_BUTTON_UP, true);
                    }
                    else if (event.jaxis.value < JOYSTICK_LOW &&
                             event.jaxis.value > -JOYSTICK_LOW) {
                        set_button_state(&buttons, MULTIPLAYER_BUTTON_DOWN, false);
                        set_button_state(&buttons, MULTIPLAYER_BUTTON_UP, false);
                    }
                    input_handled = true;
                }
            }
            else if (event.type == SDL_JOYHATMOTION &&
                     joypad_player_for_instance(event.jhat.which) >= 0) {
                uint8_t value = event.jhat.value;
                int8_t updown =
                    value == SDL_HAT_LEFTUP || value == SDL_HAT_UP ||
                    value == SDL_HAT_RIGHTUP? -1 :
                    (value == SDL_HAT_LEFTDOWN || value == SDL_HAT_DOWN ||
                     value == SDL_HAT_RIGHTDOWN? 1 : 0);
                int8_t leftright =
                    value == SDL_HAT_LEFTUP || value == SDL_HAT_LEFT ||
                    value == SDL_HAT_LEFTDOWN? -1 :
                    (value == SDL_HAT_RIGHTUP || value == SDL_HAT_RIGHT ||
                     value == SDL_HAT_RIGHTDOWN? 1 : 0);
                set_button_state(&buttons, MULTIPLAYER_BUTTON_LEFT, leftright == -1);
                set_button_state(&buttons, MULTIPLAYER_BUTTON_RIGHT, leftright == 1);
                set_button_state(&buttons, MULTIPLAYER_BUTTON_UP, updown == -1);
                set_button_state(&buttons, MULTIPLAYER_BUTTON_DOWN, updown == 1);
                input_handled = true;
            }

            if (input_handled && buttons != previous_buttons) {
                last_input_event_time = monotonic_time_us();
                SDL_LockMutex(network_mutex);
                network.buttons = buttons;
                network.input_event_timestamp_us = last_input_event_time;
                network.input_changed = true;
                SDL_UnlockMutex(network_mutex);
                render_needed = true;
            }
        }

        bool presenting_new_frame = false;
        RemoteFrameTelemetry frame_telemetry = {0};
        RemoteClockSync frame_clock_sync = {0};
        uint64_t upload_begin_time = 0;
        uint64_t upload_end_time = 0;
        SDL_LockMutex(network_mutex);
        if (video.frame_ready) {
            if (!video_texture ||
                texture_width != video.completed_width ||
                texture_height != video.completed_height) {
                bool first_video_texture = video_texture == NULL;
                if (video_texture) {
                    SDL_DestroyTexture(video_texture);
                }
                video_texture = SDL_CreateTexture(client_renderer,
                                                  SDL_PIXELFORMAT_ABGR8888,
                                                  SDL_TEXTUREACCESS_STREAMING,
                                                  video.completed_width,
                                                  video.completed_height);
                texture_width = video.completed_width;
                texture_height = video.completed_height;
                SDL_SetWindowMinimumSize(client_window,
                                         video.completed_width,
                                         video.completed_height);
                if (first_video_texture) {
                    SDL_SetWindowSize(client_window,
                                      video.completed_width * 3,
                                      video.completed_height * 3);
                }
            }
            upload_begin_time = monotonic_time_us();
            SDL_UpdateTexture(video_texture,
                              NULL,
                              video.frame_buffers[video.completed_buffer],
                              video.completed_width * 4);
            upload_end_time = monotonic_time_us();
            frame_telemetry = video.completed_telemetry;
            frame_clock_sync = clock_sync;
            presenting_new_frame = true;
            video.frame_ready = false;
            render_needed = true;
        }
        SDL_UnlockMutex(network_mutex);
        if (render_needed) {
            uint64_t present_begin_time = monotonic_time_us();
            render_client(client_renderer, video_texture, buttons);
            uint64_t present_end_time = monotonic_time_us();
            if (presenting_new_frame) {
                record_presented_frame_latency(&latency,
                                               &frame_clock_sync,
                                               &frame_telemetry,
                                               upload_begin_time,
                                               upload_end_time,
                                               present_begin_time,
                                               present_end_time);
            }
            render_needed = false;
        }
        uint64_t now = monotonic_time_us();
        if (!last_title_update_time || now - last_title_update_time >= 500000) {
            SDL_LockMutex(network_mutex);
            RemoteClockSync title_clock_sync = clock_sync;
            uint64_t title_video_drops = video.frames_dropped;
            SDL_UnlockMutex(network_mutex);
            update_latency_title(client_window,
                                 &title_clock_sync,
                                 &latency,
                                 title_video_drops,
                                 &audio);
            last_title_update_time = now;
        }
        SDL_Delay(1);
    }

    SDL_LockMutex(network_mutex);
    network.running = false;
    SDL_UnlockMutex(network_mutex);
    SDL_WaitThread(network_thread, NULL);
    uint32_t sequence = network.sequence;
    if (audio.device) {
        SDL_PauseAudioDevice(audio.device, 1);
    }
    uint32_t final_audio_buffer_ms = remote_audio_buffered_ms(&audio, NULL);
    fprintf(stderr,
            "[SameBoy Link][frontend] remote_input_client stopped packets_sent=%u video_frames=%llu video_dropped=%llu video_rejected=%llu video_superseded=%llu video_encoded_ratio=%.3f audio_codec=%s audio_callback_frames=%u audio_packets=%llu audio_dropped=%llu audio_stale=%llu audio_underflows=%llu audio_trims=%llu audio_frames_trimmed=%llu audio_buffer_ms=%u audio_target_ms=%u audio_max_buffer_ms=%u audio_rate_ppm=%.0f audio_max_arrival_gap_ms=%.3f audio_payload_bytes=%llu audio_average_payload_bytes=%.1f audio_average_decode_us=%.2f audio_plc_frames=%llu audio_decode_errors=%llu clock_samples=%llu rtt_ms=%.3f jitter_ms=%.3f clock_uncertainty_ms=%.3f latency_samples=%llu latency_latest_ms=%.3f latency_average_ms=%.3f latency_maximum_ms=%.3f\n",
            sequence,
            (unsigned long long)video.frames_completed,
            (unsigned long long)video.frames_dropped,
            (unsigned long long)video.packets_rejected,
            (unsigned long long)video.frames_superseded_before_present,
            video.raw_bytes_completed?
                (double)video.encoded_bytes_completed / video.raw_bytes_completed : 0.0,
            remote_audio_codec_name(audio.codec),
            audio.callback_frames,
            (unsigned long long)audio.packets_received,
            (unsigned long long)audio.packets_dropped,
            (unsigned long long)audio.packets_stale,
            (unsigned long long)audio.underflows,
            (unsigned long long)audio.trim_events,
            (unsigned long long)audio.frames_trimmed,
            final_audio_buffer_ms,
            audio.target_buffer_ms,
            audio.sample_rate? audio.maximum_buffered_frames * 1000 /
                audio.sample_rate : 0,
            audio.rate_correction * 1000000.0,
            audio.maximum_arrival_gap_us / 1000.0,
            (unsigned long long)audio.payload_bytes,
            audio.packets_received?
                (double)audio.payload_bytes / audio.packets_received : 0.0,
            audio.packets_received?
                (double)audio.decode_time_us / audio.packets_received : 0.0,
            (unsigned long long)audio.plc_frames,
            (unsigned long long)audio.decode_errors,
            (unsigned long long)clock_sync.responses,
            clock_sync.synchronized? clock_sync.smoothed_rtt_us / 1000.0 : -1.0,
            clock_sync.synchronized? clock_sync.jitter_us / 1000.0 : -1.0,
            clock_sync.synchronized? clock_sync.min_rtt_us / 2000.0 : -1.0,
            (unsigned long long)latency.samples,
            latency.latest_total_ms,
            latency.average_total_ms,
            latency.maximum_total_ms);
    if (audio.device) {
        SDL_CloseAudioDevice(audio.device);
    }
#ifdef ENABLE_REMOTE_OPUS
    if (audio.decoder) {
        opus_decoder_destroy(audio.decoder);
    }
#endif
    SDL_free(audio.ring_samples);
    if (video_texture) {
        SDL_DestroyTexture(video_texture);
    }
    SDL_DestroyTexture(texture);
    SDL_FreeFormat(pixel_format);
    texture = NULL;
    pixel_format = NULL;
    renderer = NULL;
    window = NULL;
    SDL_DestroyMutex(network_mutex);
    SDL_DestroyRenderer(client_renderer);
    SDL_DestroyWindow(client_window);
    remote_udp_close(&transport);
    if (!disconnected_to_frontend) {
        SDL_Quit();
        return 0;
    }
    return REMOTE_PLAY_CLIENT_DISCONNECTED;
}
