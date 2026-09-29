#ifndef GZIP_DECODE_H
#define GZIP_DECODE_H

#include "format.h"

#ifdef __cplusplus
extern "C" {
#endif

int gzip_decompress_file(const char *input_path, const char *output_path,
                         const CodecOptions *options, CodecStats *stats,
                         char *error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
