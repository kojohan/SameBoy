#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

size_t remote_video_rle_encode_rgba8(uint8_t *output,
                                     size_t output_capacity,
                                     const uint8_t *input,
                                     size_t pixel_count);
bool remote_video_rle_decode_rgba8(uint8_t *output,
                                   size_t output_size,
                                   const uint8_t *input,
                                   size_t input_size);
