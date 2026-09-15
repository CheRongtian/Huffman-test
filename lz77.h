#ifndef LZ77_H
#define LZ77_H

#include <stddef.h>
#include <stdint.h>

#define LZ77_WINDOW_SIZE 32768U
#define LZ77_MIN_MATCH 3U
#define LZ77_MAX_MATCH 258U

int lz77_compress(const uint8_t *input, size_t input_size,
                  uint8_t **output, size_t *output_size);

int lz77_decompress(const uint8_t *input, size_t input_size,
                    uint8_t *output, size_t output_capacity,
                    size_t *output_size);

#endif
