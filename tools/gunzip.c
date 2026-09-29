#include "gzip/gzip_decode.h"

#include <inttypes.h>
#include <stdio.h>

static void print_usage(const char *program)
{
    fprintf(stderr, "Usage: %s <input.gz> <output>\n", program);
}

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        print_usage(argv[0]);
        return 2;
    }

    CodecStats stats;
    char error[256] = {0};
    if (!gzip_decompress_file(argv[1], argv[2], NULL, &stats,
                              error, sizeof(error)))
    {
        fprintf(stderr, "gunzip: %s\n",
                error[0] == '\0' ? "operation failed" : error);
        return 1;
    }

    printf("Compressed size: %" PRIu64 " bytes\n", stats.input_size);
    printf("Restored size:   %" PRIu64 " bytes\n", stats.output_size);
    printf("Blocks:          %" PRIu32 " compressed, %" PRIu32 " stored\n",
           stats.compressed_blocks, stats.stored_blocks);
    return 0;
}
