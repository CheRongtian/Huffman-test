#ifndef FORMAT_H
#define FORMAT_H

#include <stddef.h>
#include <stdint.h>

typedef struct
{
    uint64_t input_size;
    uint64_t output_size;
    uint32_t block_count;
    uint32_t compressed_blocks;
    uint32_t stored_blocks;
} CodecStats;

int mgz_compress_file(const char *input_path, const char *output_path,
                      CodecStats *stats, char *error, size_t error_size);

int mgz_decompress_file(const char *input_path, const char *output_path,
                        CodecStats *stats, char *error, size_t error_size);

#endif
