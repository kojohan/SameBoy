#include "video_codec.h"

#include <string.h>

static size_t equal_pixel_run(const uint8_t *input, size_t pixel_count, size_t start)
{
    size_t run = 1;
    while (start + run < pixel_count && run < 128 &&
           memcmp(input + start * 4, input + (start + run) * 4, 4) == 0) {
        run++;
    }
    return run;
}

size_t remote_video_rle_encode_rgba8(uint8_t *output,
                                     size_t output_capacity,
                                     const uint8_t *input,
                                     size_t pixel_count)
{
    if (!output || !input || !pixel_count) {
        return 0;
    }

    size_t input_pixel = 0;
    size_t output_offset = 0;
    while (input_pixel < pixel_count) {
        size_t run = equal_pixel_run(input, pixel_count, input_pixel);
        if (run >= 2) {
            if (output_offset + 5 > output_capacity) {
                return 0;
            }
            output[output_offset++] = 0x80 | (uint8_t)(run - 1);
            memcpy(output + output_offset, input + input_pixel * 4, 4);
            output_offset += 4;
            input_pixel += run;
            continue;
        }

        size_t literal_start = input_pixel++;
        while (input_pixel < pixel_count && input_pixel - literal_start < 128 &&
               equal_pixel_run(input, pixel_count, input_pixel) < 2) {
            input_pixel++;
        }
        size_t literal_count = input_pixel - literal_start;
        size_t literal_bytes = literal_count * 4;
        if (output_offset + 1 + literal_bytes > output_capacity) {
            return 0;
        }
        output[output_offset++] = (uint8_t)(literal_count - 1);
        memcpy(output + output_offset, input + literal_start * 4, literal_bytes);
        output_offset += literal_bytes;
    }
    return output_offset;
}

bool remote_video_rle_decode_rgba8(uint8_t *output,
                                   size_t output_size,
                                   const uint8_t *input,
                                   size_t input_size)
{
    if (!output || !input || !output_size || output_size % 4) {
        return false;
    }

    size_t input_offset = 0;
    size_t output_offset = 0;
    while (input_offset < input_size) {
        uint8_t control = input[input_offset++];
        size_t pixel_count = (control & 0x7F) + 1;
        size_t byte_count = pixel_count * 4;
        if (output_offset + byte_count > output_size) {
            return false;
        }

        if (control & 0x80) {
            if (input_offset + 4 > input_size) {
                return false;
            }
            for (size_t pixel = 0; pixel < pixel_count; pixel++) {
                memcpy(output + output_offset + pixel * 4, input + input_offset, 4);
            }
            input_offset += 4;
        }
        else {
            if (input_offset + byte_count > input_size) {
                return false;
            }
            memcpy(output + output_offset, input + input_offset, byte_count);
            input_offset += byte_count;
        }
        output_offset += byte_count;
    }
    return output_offset == output_size;
}
