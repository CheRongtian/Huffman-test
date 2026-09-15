#ifndef FORMAT_H
#define FORMAT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint64_t input_size;
    uint64_t output_size;
    uint32_t block_count;
    uint32_t compressed_blocks;
    uint32_t stored_blocks;
} CodecStats;

typedef int (*CodecProgressCallback)(uint64_t completed_bytes,
                                     uint64_t total_bytes,
                                     void *user_data);

typedef struct
{
    CodecProgressCallback progress;
    void *user_data;
} CodecOptions;

int mgz_compress_file(const char *input_path, const char *output_path,
                      const CodecOptions *options, CodecStats *stats,
                      char *error, size_t error_size);

int mgz_decompress_file(const char *input_path, const char *output_path,
                        const CodecOptions *options, CodecStats *stats,
                        char *error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
