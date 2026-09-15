#include "format.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static void print_usage(const char *program)
{
    fprintf(stderr,
            "Usage:\n"
            "  %s -c <input> <output.mgz>\n"
            "  %s -d <input.mgz> <output>\n",
            program, program);
}

static void print_stats(const CodecStats *stats, double elapsed,
                        int compressing)
{
    if (compressing)
    {
        double saving = stats->input_size == 0
            ? 0.0
            : (1.0 - (double)stats->output_size /
                         (double)stats->input_size) * 100.0;

        printf("Original size:   %" PRIu64 " bytes\n", stats->input_size);
        printf("Compressed size: %" PRIu64 " bytes\n", stats->output_size);
        printf("Space saving:    %.2f%%\n", saving);
    }
    else
    {
        printf("Compressed size: %" PRIu64 " bytes\n", stats->input_size);
        printf("Restored size:   %" PRIu64 " bytes\n", stats->output_size);
    }

    printf("Blocks:          %" PRIu32 " compressed, %" PRIu32 " stored\n",
           stats->compressed_blocks, stats->stored_blocks);
    printf("Elapsed:         %.3f seconds\n", elapsed);
}

int main(int argc, char **argv)
{
    if (argc == 2 &&
        (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0))
    {
        print_usage(argv[0]);
        return 0;
    }

    if (argc != 4 ||
        (strcmp(argv[1], "-c") != 0 && strcmp(argv[1], "-d") != 0))
    {
        print_usage(argv[0]);
        return 2;
    }

    CodecStats stats;
    char error[256] = {0};
    clock_t started = clock();
    int compressing = strcmp(argv[1], "-c") == 0;
    int success = compressing
        ? mgz_compress_file(argv[2], argv[3], NULL, &stats,
                            error, sizeof(error))
        : mgz_decompress_file(argv[2], argv[3], NULL, &stats,
                              error, sizeof(error));
    double elapsed = (double)(clock() - started) / (double)CLOCKS_PER_SEC;

    if (!success)
    {
        fprintf(stderr, "huff: %s\n", error[0] == '\0' ? "operation failed" : error);
        return 1;
    }

    print_stats(&stats, elapsed, compressing);
    return 0;
}
