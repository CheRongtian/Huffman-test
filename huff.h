#ifndef HUFF_H
#define HUFF_H

#include <stddef.h>
#include <stdint.h>

#define HUFFMAN_SYMBOLS 256U
#define HUFFMAN_MAX_CODE_BITS 255U

int huffman_compress(const uint8_t *input, size_t input_size,
                     uint8_t code_lengths[HUFFMAN_SYMBOLS],
                     uint8_t **output, size_t *output_size,
                     uint64_t *output_bit_count);

int huffman_decompress(const uint8_t *input, size_t input_size,
                       uint64_t input_bit_count,
                       const uint8_t code_lengths[HUFFMAN_SYMBOLS],
                       uint8_t *output, size_t output_size);

#endif
