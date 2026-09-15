#include "format.h"

#include "checksum.h"
#include "huff.h"
#include "lz77.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define MGZ_VERSION 1U
#define MGZ_HEADER_SIZE 32U
#define MGZ_BLOCK_HEADER_SIZE 12U
#define MGZ_BLOCK_SIZE 65536U
#define MGZ_HUFFMAN_HEADER_SIZE (4U + 8U + HUFFMAN_SYMBOLS)
#define MGZ_METHOD_STORED 0U
#define MGZ_METHOD_LZ77_HUFFMAN 1U

static const uint8_t mgz_magic[4] = {'M', 'G', 'Z', '1'};

static uint64_t get_path_size(const char *path)
{
#if defined(_WIN32)
    struct _stat64 info;
    if (_stat64(path, &info) != 0 || info.st_size < 0) return 0;
#else
    struct stat info;
    if (stat(path, &info) != 0 || info.st_size < 0) return 0;
#endif
    return (uint64_t)info.st_size;
}

static void clear_stats(CodecStats *stats)
{
    if (stats != NULL) memset(stats, 0, sizeof(*stats));
}

static void set_error(char *error, size_t error_size, const char *message)
{
    if (error == NULL || error_size == 0) return;
    snprintf(error, error_size, "%s", message);
}

static void set_system_error(char *error, size_t error_size,
                             const char *operation)
{
    if (error == NULL || error_size == 0) return;
    snprintf(error, error_size, "%s: %s", operation, strerror(errno));
}

static int report_progress(const CodecOptions *options,
                           uint64_t completed_bytes,
                           uint64_t total_bytes,
                           char *error, size_t error_size)
{
    if (options == NULL || options->progress == NULL) return 1;
    if (options->progress(completed_bytes, total_bytes,
                          options->user_data))
    {
        return 1;
    }

    set_error(error, error_size, "operation cancelled");
    return 0;
}

static int write_bytes(FILE *file, const void *data, size_t size)
{
    return size == 0 || fwrite(data, 1, size, file) == size;
}

static int read_bytes(FILE *file, void *data, size_t size)
{
    return size == 0 || fread(data, 1, size, file) == size;
}

static int write_u8(FILE *file, uint8_t value)
{
    return write_bytes(file, &value, 1);
}

static int write_u16_le(FILE *file, uint16_t value)
{
    uint8_t bytes[2] = {
        (uint8_t)(value & 0xFFU),
        (uint8_t)((value >> 8U) & 0xFFU)
    };
    return write_bytes(file, bytes, sizeof(bytes));
}

static int write_u32_le(FILE *file, uint32_t value)
{
    uint8_t bytes[4];
    for (unsigned int i = 0; i < 4; i++)
    {
        bytes[i] = (uint8_t)((value >> (i * 8U)) & 0xFFU);
    }
    return write_bytes(file, bytes, sizeof(bytes));
}

static int write_u64_le(FILE *file, uint64_t value)
{
    uint8_t bytes[8];
    for (unsigned int i = 0; i < 8; i++)
    {
        bytes[i] = (uint8_t)((value >> (i * 8U)) & 0xFFU);
    }
    return write_bytes(file, bytes, sizeof(bytes));
}

static int read_u8(FILE *file, uint8_t *value)
{
    return read_bytes(file, value, 1);
}

static int read_u16_le(FILE *file, uint16_t *value)
{
    uint8_t bytes[2];
    if (!read_bytes(file, bytes, sizeof(bytes))) return 0;
    *value = (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U);
    return 1;
}

static int read_u32_le(FILE *file, uint32_t *value)
{
    uint8_t bytes[4];
    if (!read_bytes(file, bytes, sizeof(bytes))) return 0;

    *value = 0;
    for (unsigned int i = 0; i < 4; i++)
    {
        *value |= (uint32_t)bytes[i] << (i * 8U);
    }
    return 1;
}

static int read_u64_le(FILE *file, uint64_t *value)
{
    uint8_t bytes[8];
    if (!read_bytes(file, bytes, sizeof(bytes))) return 0;

    *value = 0;
    for (unsigned int i = 0; i < 8; i++)
    {
        *value |= (uint64_t)bytes[i] << (i * 8U);
    }
    return 1;
}

static uint32_t load_u32_le(const uint8_t *bytes)
{
    uint32_t value = 0;
    for (unsigned int i = 0; i < 4; i++)
    {
        value |= (uint32_t)bytes[i] << (i * 8U);
    }
    return value;
}

static uint64_t load_u64_le(const uint8_t *bytes)
{
    uint64_t value = 0;
    for (unsigned int i = 0; i < 8; i++)
    {
        value |= (uint64_t)bytes[i] << (i * 8U);
    }
    return value;
}

static int write_file_header(FILE *file, uint64_t original_size,
                             uint32_t checksum, uint32_t block_count)
{
    return write_bytes(file, mgz_magic, sizeof(mgz_magic)) &&
           write_u8(file, MGZ_VERSION) &&
           write_u8(file, 0) &&
           write_u16_le(file, MGZ_HEADER_SIZE) &&
           write_u32_le(file, MGZ_BLOCK_SIZE) &&
           write_u64_le(file, original_size) &&
           write_u32_le(file, checksum) &&
           write_u32_le(file, block_count) &&
           write_u32_le(file, 0);
}

static int write_block_header(FILE *file, uint8_t method,
                              uint32_t original_size, uint32_t payload_size)
{
    static const uint8_t reserved[3] = {0, 0, 0};
    return write_u8(file, method) &&
           write_bytes(file, reserved, sizeof(reserved)) &&
           write_u32_le(file, original_size) &&
           write_u32_le(file, payload_size);
}

static int read_file_header(FILE *file, uint64_t *original_size,
                            uint32_t *checksum, uint32_t *block_count,
                            char *error, size_t error_size)
{
    uint8_t magic[4];
    uint8_t version;
    uint8_t flags;
    uint16_t header_size;
    uint32_t block_size;
    uint32_t reserved;

    if (!read_bytes(file, magic, sizeof(magic)) || !read_u8(file, &version) ||
        !read_u8(file, &flags) || !read_u16_le(file, &header_size) ||
        !read_u32_le(file, &block_size) ||
        !read_u64_le(file, original_size) ||
        !read_u32_le(file, checksum) || !read_u32_le(file, block_count) ||
        !read_u32_le(file, &reserved))
    {
        set_error(error, error_size, "truncated MGZ header");
        return 0;
    }

    if (memcmp(magic, mgz_magic, sizeof(magic)) != 0)
    {
        set_error(error, error_size, "invalid MGZ magic number");
        return 0;
    }
    if (version != MGZ_VERSION)
    {
        set_error(error, error_size, "unsupported MGZ version");
        return 0;
    }
    if (flags != 0 || header_size != MGZ_HEADER_SIZE ||
        block_size != MGZ_BLOCK_SIZE || reserved != 0)
    {
        set_error(error, error_size, "invalid MGZ header fields");
        return 0;
    }

    uint64_t expected_blocks = *original_size == 0
        ? 0
        : ((*original_size - 1U) / MGZ_BLOCK_SIZE) + 1U;
    if (expected_blocks != *block_count)
    {
        set_error(error, error_size, "inconsistent MGZ size and block count");
        return 0;
    }

    return 1;
}

static int read_block_header(FILE *file, uint8_t *method,
                             uint32_t *original_size, uint32_t *payload_size,
                             char *error, size_t error_size)
{
    uint8_t reserved[3];
    if (!read_u8(file, method) || !read_bytes(file, reserved, sizeof(reserved)) ||
        !read_u32_le(file, original_size) ||
        !read_u32_le(file, payload_size))
    {
        set_error(error, error_size, "truncated MGZ block header");
        return 0;
    }

    if (reserved[0] != 0 || reserved[1] != 0 || reserved[2] != 0)
    {
        set_error(error, error_size, "invalid MGZ block flags");
        return 0;
    }
    return 1;
}

static int decode_compressed_block(const uint8_t *payload,
                                   size_t payload_size,
                                   uint8_t *output,
                                   size_t output_size,
                                   char *error, size_t error_size)
{
    if (payload_size <= MGZ_HUFFMAN_HEADER_SIZE)
    {
        set_error(error, error_size, "truncated compressed block");
        return 0;
    }

    uint32_t token_size = load_u32_le(payload);
    uint64_t bit_count = load_u64_le(payload + 4U);
    const uint8_t *code_lengths = payload + 12U;
    const uint8_t *bitstream = payload + MGZ_HUFFMAN_HEADER_SIZE;
    size_t bitstream_size = payload_size - MGZ_HUFFMAN_HEADER_SIZE;

    if (token_size == 0 || token_size > output_size * 2U)
    {
        set_error(error, error_size, "invalid LZ77 token stream size");
        return 0;
    }
    if (bit_count == 0 || bitstream_size > UINT64_MAX / 8U ||
        bit_count > (uint64_t)bitstream_size * 8U ||
        ((bit_count - 1U) / 8U) + 1U != bitstream_size)
    {
        set_error(error, error_size, "invalid Huffman bit count");
        return 0;
    }

    unsigned int used_bits = (unsigned int)(bit_count & 7U);
    if (used_bits != 0)
    {
        unsigned int padding_bits = 8U - used_bits;
        uint8_t padding_mask = (uint8_t)((1U << padding_bits) - 1U);
        if ((bitstream[bitstream_size - 1U] & padding_mask) != 0)
        {
            set_error(error, error_size, "nonzero Huffman padding bits");
            return 0;
        }
    }

    uint8_t *tokens = malloc(token_size);
    if (tokens == NULL)
    {
        set_error(error, error_size, "out of memory for token stream");
        return 0;
    }

    int result = 0;
    if (!huffman_decompress(bitstream, bitstream_size, bit_count,
                            code_lengths, tokens, token_size))
    {
        set_error(error, error_size, "invalid canonical Huffman stream");
        goto cleanup;
    }

    size_t decoded_size = 0;
    if (!lz77_decompress(tokens, token_size, output, output_size,
                         &decoded_size) || decoded_size != output_size)
    {
        set_error(error, error_size, "invalid LZ77 token stream");
        goto cleanup;
    }

    result = 1;

cleanup:
    free(tokens);
    return result;
}

int mgz_compress_file(const char *input_path, const char *output_path,
                      const CodecOptions *options, CodecStats *stats,
                      char *error, size_t error_size)
{
    clear_stats(stats);
    if (input_path == NULL || output_path == NULL ||
        strcmp(input_path, output_path) == 0)
    {
        set_error(error, error_size, "input and output paths must differ");
        return 0;
    }

    FILE *input = fopen(input_path, "rb");
    if (input == NULL)
    {
        set_system_error(error, error_size, "cannot open input file");
        return 0;
    }

    FILE *output = fopen(output_path, "wb");
    if (output == NULL)
    {
        set_system_error(error, error_size, "cannot open output file");
        fclose(input);
        return 0;
    }

    uint8_t *block = malloc(MGZ_BLOCK_SIZE);
    uint8_t *tokens = NULL;
    uint8_t *bits = NULL;
    int success = 0;
    uint64_t original_size = 0;
    uint64_t written_size = MGZ_HEADER_SIZE;
    uint32_t block_count = 0;
    uint32_t compressed_blocks = 0;
    uint32_t stored_blocks = 0;
    uint32_t crc = crc32_begin();
    uint64_t input_size = get_path_size(input_path);

    if (block == NULL)
    {
        set_error(error, error_size, "out of memory for input block");
        goto cleanup;
    }
    if (!write_file_header(output, 0, 0, 0))
    {
        set_system_error(error, error_size, "cannot write MGZ header");
        goto cleanup;
    }
    if (!report_progress(options, 0, input_size, error, error_size))
    {
        goto cleanup;
    }

    for (;;)
    {
        size_t block_size = fread(block, 1, MGZ_BLOCK_SIZE, input);
        if (block_size == 0)
        {
            if (ferror(input))
            {
                set_system_error(error, error_size, "cannot read input file");
                goto cleanup;
            }
            break;
        }

        if (block_count == UINT32_MAX ||
            UINT64_MAX - original_size < block_size)
        {
            set_error(error, error_size, "input file is too large for MGZ v1");
            goto cleanup;
        }

        crc = crc32_update(crc, block, block_size);
        original_size += block_size;

        size_t token_size = 0;
        if (!lz77_compress(block, block_size, &tokens, &token_size))
        {
            set_error(error, error_size, "LZ77 compression failed");
            goto cleanup;
        }

        uint8_t code_lengths[HUFFMAN_SYMBOLS];
        size_t bitstream_size = 0;
        uint64_t bit_count = 0;
        if (!huffman_compress(tokens, token_size, code_lengths, &bits,
                              &bitstream_size, &bit_count))
        {
            set_error(error, error_size, "Huffman compression failed");
            goto cleanup;
        }

        size_t compressed_size = MGZ_HUFFMAN_HEADER_SIZE + bitstream_size;
        if (compressed_size < block_size)
        {
            if (!write_block_header(output, MGZ_METHOD_LZ77_HUFFMAN,
                                    (uint32_t)block_size,
                                    (uint32_t)compressed_size) ||
                !write_u32_le(output, (uint32_t)token_size) ||
                !write_u64_le(output, bit_count) ||
                !write_bytes(output, code_lengths, HUFFMAN_SYMBOLS) ||
                !write_bytes(output, bits, bitstream_size))
            {
                set_system_error(error, error_size, "cannot write compressed block");
                goto cleanup;
            }
            compressed_blocks++;
            written_size += MGZ_BLOCK_HEADER_SIZE + compressed_size;
        }
        else
        {
            if (!write_block_header(output, MGZ_METHOD_STORED,
                                    (uint32_t)block_size,
                                    (uint32_t)block_size) ||
                !write_bytes(output, block, block_size))
            {
                set_system_error(error, error_size, "cannot write stored block");
                goto cleanup;
            }
            stored_blocks++;
            written_size += MGZ_BLOCK_HEADER_SIZE + block_size;
        }

        free(tokens);
        tokens = NULL;
        free(bits);
        bits = NULL;
        block_count++;

        if (!report_progress(options, original_size, input_size,
                             error, error_size))
        {
            goto cleanup;
        }
    }

    crc = crc32_finish(crc);
    if (fseek(output, 0, SEEK_SET) != 0 ||
        !write_file_header(output, original_size, crc, block_count) ||
        fflush(output) != 0)
    {
        set_system_error(error, error_size, "cannot finalize MGZ file");
        goto cleanup;
    }

    success = 1;
    if (stats != NULL)
    {
        stats->input_size = original_size;
        stats->output_size = written_size;
        stats->block_count = block_count;
        stats->compressed_blocks = compressed_blocks;
        stats->stored_blocks = stored_blocks;
    }

cleanup:
    free(bits);
    free(tokens);
    free(block);
    if (fclose(input) != 0 && success)
    {
        set_system_error(error, error_size, "cannot close input file");
        success = 0;
    }
    if (fclose(output) != 0 && success)
    {
        set_system_error(error, error_size, "cannot close output file");
        success = 0;
    }
    if (!success) remove(output_path);
    return success;
}

int mgz_decompress_file(const char *input_path, const char *output_path,
                        const CodecOptions *options, CodecStats *stats,
                        char *error, size_t error_size)
{
    clear_stats(stats);
    if (input_path == NULL || output_path == NULL ||
        strcmp(input_path, output_path) == 0)
    {
        set_error(error, error_size, "input and output paths must differ");
        return 0;
    }

    FILE *input = fopen(input_path, "rb");
    if (input == NULL)
    {
        set_system_error(error, error_size, "cannot open input file");
        return 0;
    }

    uint64_t original_size;
    uint32_t expected_crc;
    uint32_t block_count;
    if (!read_file_header(input, &original_size, &expected_crc, &block_count,
                          error, error_size))
    {
        fclose(input);
        return 0;
    }

    FILE *output = fopen(output_path, "wb");
    if (output == NULL)
    {
        set_system_error(error, error_size, "cannot open output file");
        fclose(input);
        return 0;
    }

    uint8_t *decoded = malloc(MGZ_BLOCK_SIZE);
    uint8_t *payload = NULL;
    int success = 0;
    uint64_t decoded_total = 0;
    uint64_t read_size = MGZ_HEADER_SIZE;
    uint32_t compressed_blocks = 0;
    uint32_t stored_blocks = 0;
    uint32_t crc = crc32_begin();

    if (decoded == NULL)
    {
        set_error(error, error_size, "out of memory for output block");
        goto cleanup;
    }
    if (!report_progress(options, 0, original_size, error, error_size))
    {
        goto cleanup;
    }

    for (uint32_t block_index = 0; block_index < block_count; block_index++)
    {
        uint8_t method;
        uint32_t block_size;
        uint32_t payload_size;
        if (!read_block_header(input, &method, &block_size, &payload_size,
                               error, error_size))
        {
            goto cleanup;
        }

        uint64_t expected_block_size = block_index + 1U < block_count
            ? MGZ_BLOCK_SIZE
            : original_size - decoded_total;
        if (block_size == 0 || block_size != expected_block_size)
        {
            set_error(error, error_size, "invalid MGZ block size");
            goto cleanup;
        }

        if ((method == MGZ_METHOD_STORED && payload_size != block_size) ||
            (method == MGZ_METHOD_LZ77_HUFFMAN &&
             (payload_size <= MGZ_HUFFMAN_HEADER_SIZE ||
              payload_size >= block_size)) ||
            (method != MGZ_METHOD_STORED &&
             method != MGZ_METHOD_LZ77_HUFFMAN))
        {
            set_error(error, error_size, "invalid MGZ block method or payload size");
            goto cleanup;
        }

        payload = malloc(payload_size);
        if (payload == NULL)
        {
            set_error(error, error_size, "out of memory for block payload");
            goto cleanup;
        }
        if (!read_bytes(input, payload, payload_size))
        {
            set_error(error, error_size, "truncated MGZ block payload");
            goto cleanup;
        }

        if (method == MGZ_METHOD_STORED)
        {
            memcpy(decoded, payload, block_size);
            stored_blocks++;
        }
        else
        {
            if (!decode_compressed_block(payload, payload_size, decoded,
                                         block_size, error, error_size))
            {
                goto cleanup;
            }
            compressed_blocks++;
        }

        if (!write_bytes(output, decoded, block_size))
        {
            set_system_error(error, error_size, "cannot write output file");
            goto cleanup;
        }

        crc = crc32_update(crc, decoded, block_size);
        decoded_total += block_size;
        read_size += MGZ_BLOCK_HEADER_SIZE + payload_size;
        free(payload);
        payload = NULL;

        if (!report_progress(options, decoded_total, original_size,
                             error, error_size))
        {
            goto cleanup;
        }
    }

    if (decoded_total != original_size)
    {
        set_error(error, error_size, "decoded size does not match MGZ header");
        goto cleanup;
    }

    int trailing = fgetc(input);
    if (trailing != EOF || ferror(input))
    {
        set_error(error, error_size, "unexpected trailing data in MGZ file");
        goto cleanup;
    }

    crc = crc32_finish(crc);
    if (crc != expected_crc)
    {
        set_error(error, error_size, "CRC32 mismatch: compressed file is corrupted");
        goto cleanup;
    }
    if (fflush(output) != 0)
    {
        set_system_error(error, error_size, "cannot finalize output file");
        goto cleanup;
    }

    success = 1;
    if (stats != NULL)
    {
        stats->input_size = read_size;
        stats->output_size = decoded_total;
        stats->block_count = block_count;
        stats->compressed_blocks = compressed_blocks;
        stats->stored_blocks = stored_blocks;
    }

cleanup:
    free(payload);
    free(decoded);
    if (fclose(input) != 0 && success)
    {
        set_system_error(error, error_size, "cannot close input file");
        success = 0;
    }
    if (fclose(output) != 0 && success)
    {
        set_system_error(error, error_size, "cannot close output file");
        success = 0;
    }
    if (!success) remove(output_path);
    return success;
}
