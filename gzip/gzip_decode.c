#include "gzip_decode.h"

#include "checksum.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define GZIP_FLAG_HEADER_CRC 0x02U
#define GZIP_FLAG_EXTRA 0x04U
#define GZIP_FLAG_NAME 0x08U
#define GZIP_FLAG_COMMENT 0x10U
#define GZIP_FLAG_RESERVED 0xE0U

#define DEFLATE_MAX_BITS 15U
#define DEFLATE_LITERAL_SYMBOLS 288U
#define DEFLATE_DISTANCE_SYMBOLS 32U
#define HUFFMAN_MAX_NODES (DEFLATE_LITERAL_SYMBOLS * 2U)
#define OUTPUT_BUFFER_SIZE 65536U
#define OUTPUT_WINDOW_SIZE 32768U

typedef struct
{
    FILE *file;
    uint64_t bytes_read;
} GzipInput;

typedef struct
{
    GzipInput *input;
    uint32_t bits;
    unsigned int bit_count;
} BitReader;

typedef struct
{
    int16_t child[2];
    int16_t symbol;
} HuffmanNode;

typedef struct
{
    HuffmanNode nodes[HUFFMAN_MAX_NODES];
    size_t node_count;
} HuffmanTree;

typedef struct
{
    FILE *file;
    GzipInput *input;
    const CodecOptions *options;
    uint64_t input_size;
    uint64_t output_size;
    uint8_t buffer[OUTPUT_BUFFER_SIZE];
    size_t buffer_size;
    uint8_t window[OUTPUT_WINDOW_SIZE];
    uint32_t crc;
} InflateOutput;

static const uint16_t length_bases[29] = {
    3, 4, 5, 6, 7, 8, 9, 10,
    11, 13, 15, 17,
    19, 23, 27, 31,
    35, 43, 51, 59,
    67, 83, 99, 115,
    131, 163, 195, 227,
    258
};

static const uint8_t length_extra_bits[29] = {
    0, 0, 0, 0, 0, 0, 0, 0,
    1, 1, 1, 1,
    2, 2, 2, 2,
    3, 3, 3, 3,
    4, 4, 4, 4,
    5, 5, 5, 5,
    0
};

static const uint16_t distance_bases[30] = {
    1, 2, 3, 4,
    5, 7,
    9, 13,
    17, 25,
    33, 49,
    65, 97,
    129, 193,
    257, 385,
    513, 769,
    1025, 1537,
    2049, 3073,
    4097, 6145,
    8193, 12289,
    16385, 24577
};

static const uint8_t distance_extra_bits[30] = {
    0, 0, 0, 0,
    1, 1,
    2, 2,
    3, 3,
    4, 4,
    5, 5,
    6, 6,
    7, 7,
    8, 8,
    9, 9,
    10, 10,
    11, 11,
    12, 12,
    13, 13
};

static const uint8_t code_length_order[19] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5,
    11, 4, 12, 3, 13, 2, 14, 1, 15
};

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

static int report_progress(const CodecOptions *options,
                           uint64_t completed_bytes,
                           uint64_t total_bytes,
                           char *error, size_t error_size)
{
    if (options == NULL || options->progress == NULL) return 1;
    if (total_bytes != 0 && completed_bytes > total_bytes)
        completed_bytes = total_bytes;
    if (options->progress(completed_bytes, total_bytes, options->user_data))
        return 1;

    set_error(error, error_size, "operation cancelled");
    return 0;
}

static int input_read_byte(GzipInput *input, uint8_t *value,
                           char *error, size_t error_size)
{
    int byte = fgetc(input->file);
    if (byte == EOF)
    {
        if (ferror(input->file))
            set_system_error(error, error_size, "cannot read GZIP file");
        else
            set_error(error, error_size, "truncated GZIP file");
        return 0;
    }

    *value = (uint8_t)byte;
    input->bytes_read++;
    return 1;
}

static int input_read_u16_le(GzipInput *input, uint16_t *value,
                             char *error, size_t error_size)
{
    uint8_t low;
    uint8_t high;
    if (!input_read_byte(input, &low, error, error_size) ||
        !input_read_byte(input, &high, error, error_size))
    {
        return 0;
    }

    *value = (uint16_t)low | ((uint16_t)high << 8U);
    return 1;
}

static int input_read_u32_le(GzipInput *input, uint32_t *value,
                             char *error, size_t error_size)
{
    uint8_t bytes[4];
    for (size_t index = 0; index < sizeof(bytes); index++)
    {
        if (!input_read_byte(input, &bytes[index], error, error_size))
            return 0;
    }

    *value = (uint32_t)bytes[0] |
             ((uint32_t)bytes[1] << 8U) |
             ((uint32_t)bytes[2] << 16U) |
             ((uint32_t)bytes[3] << 24U);
    return 1;
}

static int read_header_byte(GzipInput *input, uint8_t *value,
                            uint32_t *header_crc,
                            char *error, size_t error_size)
{
    if (!input_read_byte(input, value, error, error_size)) return 0;
    *header_crc = crc32_update(*header_crc, value, 1);
    return 1;
}

static int read_zero_terminated_header_field(GzipInput *input,
                                             uint32_t *header_crc,
                                             char *error,
                                             size_t error_size)
{
    uint8_t byte;
    do
    {
        if (!read_header_byte(input, &byte, header_crc, error, error_size))
            return 0;
    } while (byte != 0);
    return 1;
}

static int read_gzip_header(GzipInput *input,
                            char *error, size_t error_size)
{
    uint8_t fixed[10];
    uint32_t header_crc = crc32_begin();
    for (size_t index = 0; index < sizeof(fixed); index++)
    {
        if (!read_header_byte(input, &fixed[index], &header_crc,
                              error, error_size))
        {
            return 0;
        }
    }

    if (fixed[0] != 0x1FU || fixed[1] != 0x8BU)
    {
        set_error(error, error_size, "invalid GZIP magic number");
        return 0;
    }
    if (fixed[2] != 8U)
    {
        set_error(error, error_size, "unsupported GZIP compression method");
        return 0;
    }

    uint8_t flags = fixed[3];
    if ((flags & GZIP_FLAG_RESERVED) != 0)
    {
        set_error(error, error_size, "invalid reserved GZIP flags");
        return 0;
    }

    if ((flags & GZIP_FLAG_EXTRA) != 0)
    {
        uint8_t low;
        uint8_t high;
        if (!read_header_byte(input, &low, &header_crc, error, error_size) ||
            !read_header_byte(input, &high, &header_crc, error, error_size))
        {
            return 0;
        }

        uint16_t extra_size = (uint16_t)low | ((uint16_t)high << 8U);
        for (uint16_t index = 0; index < extra_size; index++)
        {
            uint8_t ignored;
            if (!read_header_byte(input, &ignored, &header_crc,
                                  error, error_size))
            {
                return 0;
            }
        }
    }

    if ((flags & GZIP_FLAG_NAME) != 0 &&
        !read_zero_terminated_header_field(input, &header_crc,
                                           error, error_size))
    {
        return 0;
    }
    if ((flags & GZIP_FLAG_COMMENT) != 0 &&
        !read_zero_terminated_header_field(input, &header_crc,
                                           error, error_size))
    {
        return 0;
    }

    if ((flags & GZIP_FLAG_HEADER_CRC) != 0)
    {
        uint16_t expected_crc;
        if (!input_read_u16_le(input, &expected_crc, error, error_size))
            return 0;
        if ((uint16_t)(crc32_finish(header_crc) & 0xFFFFU) != expected_crc)
        {
            set_error(error, error_size, "GZIP header CRC16 mismatch");
            return 0;
        }
    }

    return 1;
}

static int bit_reader_read(BitReader *reader, unsigned int bit_count,
                           uint32_t *value,
                           char *error, size_t error_size)
{
    if (bit_count > 16U)
    {
        set_error(error, error_size, "internal DEFLATE bit request is invalid");
        return 0;
    }

    while (reader->bit_count < bit_count)
    {
        uint8_t byte;
        if (!input_read_byte(reader->input, &byte, error, error_size)) return 0;
        reader->bits |= (uint32_t)byte << reader->bit_count;
        reader->bit_count += 8U;
    }

    uint32_t mask = bit_count == 0U
        ? 0U
        : ((uint32_t)1U << bit_count) - 1U;
    *value = reader->bits & mask;
    reader->bits >>= bit_count;
    reader->bit_count -= bit_count;
    return 1;
}

static void bit_reader_align_byte(BitReader *reader)
{
    unsigned int padding = reader->bit_count % 8U;
    reader->bits >>= padding;
    reader->bit_count -= padding;
}

static void huffman_initialize(HuffmanTree *tree)
{
    tree->node_count = 1;
    tree->nodes[0].child[0] = -1;
    tree->nodes[0].child[1] = -1;
    tree->nodes[0].symbol = -1;
}

static int huffman_add_node(HuffmanTree *tree, int16_t *node_index,
                            char *error, size_t error_size)
{
    if (tree->node_count >= HUFFMAN_MAX_NODES)
    {
        set_error(error, error_size, "DEFLATE Huffman tree is too large");
        return 0;
    }

    size_t index = tree->node_count++;
    tree->nodes[index].child[0] = -1;
    tree->nodes[index].child[1] = -1;
    tree->nodes[index].symbol = -1;
    *node_index = (int16_t)index;
    return 1;
}

static int huffman_build(HuffmanTree *tree,
                         const uint8_t *lengths, size_t symbol_count,
                         int allow_empty,
                         char *error, size_t error_size)
{
    uint16_t counts[DEFLATE_MAX_BITS + 1U] = {0};
    uint32_t next_code[DEFLATE_MAX_BITS + 1U] = {0};
    size_t used_symbols = 0;

    for (size_t symbol = 0; symbol < symbol_count; symbol++)
    {
        if (lengths[symbol] > DEFLATE_MAX_BITS)
        {
            set_error(error, error_size, "invalid DEFLATE Huffman code length");
            return 0;
        }
        if (lengths[symbol] != 0)
        {
            counts[lengths[symbol]]++;
            used_symbols++;
        }
    }

    huffman_initialize(tree);
    if (used_symbols == 0)
    {
        if (allow_empty) return 1;
        set_error(error, error_size, "empty DEFLATE Huffman tree");
        return 0;
    }

    int32_t available = 1;
    for (unsigned int bits = 1; bits <= DEFLATE_MAX_BITS; bits++)
    {
        available = (available << 1) - counts[bits];
        if (available < 0)
        {
            set_error(error, error_size, "oversubscribed DEFLATE Huffman tree");
            return 0;
        }
    }

    uint32_t code = 0;
    for (unsigned int bits = 1; bits <= DEFLATE_MAX_BITS; bits++)
    {
        code = (code + counts[bits - 1U]) << 1U;
        next_code[bits] = code;
    }

    for (size_t symbol = 0; symbol < symbol_count; symbol++)
    {
        unsigned int length = lengths[symbol];
        if (length == 0U) continue;

        uint32_t symbol_code = next_code[length]++;
        if (symbol_code >= ((uint32_t)1U << length))
        {
            set_error(error, error_size, "invalid DEFLATE Huffman code");
            return 0;
        }

        int16_t node = 0;
        for (int bit_index = (int)length - 1; bit_index >= 0; bit_index--)
        {
            if (tree->nodes[node].symbol >= 0)
            {
                set_error(error, error_size, "invalid DEFLATE Huffman prefix");
                return 0;
            }

            unsigned int bit = (symbol_code >> bit_index) & 1U;
            if (tree->nodes[node].child[bit] < 0)
            {
                int16_t child;
                if (!huffman_add_node(tree, &child, error, error_size))
                    return 0;
                tree->nodes[node].child[bit] = child;
            }
            node = tree->nodes[node].child[bit];
        }

        if (tree->nodes[node].symbol >= 0 ||
            tree->nodes[node].child[0] >= 0 ||
            tree->nodes[node].child[1] >= 0)
        {
            set_error(error, error_size, "duplicate DEFLATE Huffman code");
            return 0;
        }
        tree->nodes[node].symbol = (int16_t)symbol;
    }

    return 1;
}

static int huffman_decode(BitReader *reader, const HuffmanTree *tree,
                          uint16_t *symbol,
                          char *error, size_t error_size)
{
    int16_t node = 0;
    for (unsigned int depth = 0; depth < DEFLATE_MAX_BITS; depth++)
    {
        uint32_t bit;
        if (!bit_reader_read(reader, 1, &bit, error, error_size)) return 0;

        node = tree->nodes[node].child[bit];
        if (node < 0)
        {
            set_error(error, error_size, "invalid DEFLATE Huffman symbol");
            return 0;
        }
        if (tree->nodes[node].symbol >= 0)
        {
            *symbol = (uint16_t)tree->nodes[node].symbol;
            return 1;
        }
    }

    set_error(error, error_size, "DEFLATE Huffman symbol exceeds 15 bits");
    return 0;
}

static int output_flush(InflateOutput *output,
                        char *error, size_t error_size)
{
    if (output->buffer_size != 0)
    {
        if (fwrite(output->buffer, 1, output->buffer_size, output->file) !=
            output->buffer_size)
        {
            set_system_error(error, error_size, "cannot write output file");
            return 0;
        }
        output->crc = crc32_update(output->crc, output->buffer,
                                   output->buffer_size);
        output->buffer_size = 0;
    }

    return report_progress(output->options, output->input->bytes_read,
                           output->input_size, error, error_size);
}

static int output_emit(InflateOutput *output, uint8_t value,
                       char *error, size_t error_size)
{
    if (output->output_size == UINT64_MAX)
    {
        set_error(error, error_size, "GZIP output is too large");
        return 0;
    }

    output->window[output->output_size % OUTPUT_WINDOW_SIZE] = value;
    output->buffer[output->buffer_size++] = value;
    output->output_size++;

    if (output->buffer_size == OUTPUT_BUFFER_SIZE)
        return output_flush(output, error, error_size);
    return 1;
}

static int output_copy(InflateOutput *output, uint32_t distance,
                       uint32_t length,
                       char *error, size_t error_size)
{
    if (distance == 0 || distance > OUTPUT_WINDOW_SIZE ||
        distance > output->output_size)
    {
        set_error(error, error_size, "invalid DEFLATE back-reference distance");
        return 0;
    }

    for (uint32_t index = 0; index < length; index++)
    {
        uint64_t source = output->output_size - distance;
        uint8_t value = output->window[source % OUTPUT_WINDOW_SIZE];
        if (!output_emit(output, value, error, error_size)) return 0;
    }
    return 1;
}

static int build_fixed_trees(HuffmanTree *literal_tree,
                             HuffmanTree *distance_tree,
                             char *error, size_t error_size)
{
    uint8_t literal_lengths[DEFLATE_LITERAL_SYMBOLS] = {0};
    uint8_t distance_lengths[DEFLATE_DISTANCE_SYMBOLS] = {0};

    for (size_t symbol = 0; symbol <= 143; symbol++)
        literal_lengths[symbol] = 8;
    for (size_t symbol = 144; symbol <= 255; symbol++)
        literal_lengths[symbol] = 9;
    for (size_t symbol = 256; symbol <= 279; symbol++)
        literal_lengths[symbol] = 7;
    for (size_t symbol = 280; symbol < DEFLATE_LITERAL_SYMBOLS; symbol++)
        literal_lengths[symbol] = 8;
    for (size_t symbol = 0; symbol < DEFLATE_DISTANCE_SYMBOLS; symbol++)
        distance_lengths[symbol] = 5;

    return huffman_build(literal_tree, literal_lengths,
                         DEFLATE_LITERAL_SYMBOLS, 0, error, error_size) &&
           huffman_build(distance_tree, distance_lengths,
                         DEFLATE_DISTANCE_SYMBOLS, 0, error, error_size);
}

static int build_dynamic_trees(BitReader *reader,
                               HuffmanTree *literal_tree,
                               HuffmanTree *distance_tree,
                               char *error, size_t error_size)
{
    uint32_t value;
    if (!bit_reader_read(reader, 5, &value, error, error_size)) return 0;
    uint32_t literal_count = value + 257U;
    if (literal_count > 286U)
    {
        set_error(error, error_size, "invalid DEFLATE literal code count");
        return 0;
    }

    if (!bit_reader_read(reader, 5, &value, error, error_size)) return 0;
    uint32_t distance_count = value + 1U;
    if (!bit_reader_read(reader, 4, &value, error, error_size)) return 0;
    uint32_t code_length_count = value + 4U;

    uint8_t code_lengths[19] = {0};
    for (uint32_t index = 0; index < code_length_count; index++)
    {
        if (!bit_reader_read(reader, 3, &value, error, error_size)) return 0;
        code_lengths[code_length_order[index]] = (uint8_t)value;
    }

    HuffmanTree code_length_tree;
    if (!huffman_build(&code_length_tree, code_lengths, 19, 0,
                       error, error_size))
    {
        return 0;
    }

    uint8_t all_lengths[DEFLATE_LITERAL_SYMBOLS +
                        DEFLATE_DISTANCE_SYMBOLS] = {0};
    uint32_t total_count = literal_count + distance_count;
    uint32_t position = 0;
    while (position < total_count)
    {
        uint16_t symbol;
        if (!huffman_decode(reader, &code_length_tree, &symbol,
                            error, error_size))
        {
            return 0;
        }

        if (symbol <= 15U)
        {
            all_lengths[position++] = (uint8_t)symbol;
            continue;
        }

        uint32_t repeat_count;
        uint8_t repeated_length = 0;
        if (symbol == 16U)
        {
            if (position == 0)
            {
                set_error(error, error_size,
                          "DEFLATE repeat has no previous code length");
                return 0;
            }
            if (!bit_reader_read(reader, 2, &value, error, error_size))
                return 0;
            repeat_count = value + 3U;
            repeated_length = all_lengths[position - 1U];
        }
        else if (symbol == 17U)
        {
            if (!bit_reader_read(reader, 3, &value, error, error_size))
                return 0;
            repeat_count = value + 3U;
        }
        else if (symbol == 18U)
        {
            if (!bit_reader_read(reader, 7, &value, error, error_size))
                return 0;
            repeat_count = value + 11U;
        }
        else
        {
            set_error(error, error_size, "invalid DEFLATE code-length symbol");
            return 0;
        }

        if (repeat_count > total_count - position)
        {
            set_error(error, error_size, "DEFLATE code-length repeat overflows table");
            return 0;
        }
        while (repeat_count-- != 0)
            all_lengths[position++] = repeated_length;
    }

    uint8_t literal_lengths[DEFLATE_LITERAL_SYMBOLS] = {0};
    uint8_t distance_lengths[DEFLATE_DISTANCE_SYMBOLS] = {0};
    memcpy(literal_lengths, all_lengths, literal_count);
    memcpy(distance_lengths, all_lengths + literal_count, distance_count);

    if (literal_lengths[256] == 0)
    {
        set_error(error, error_size, "DEFLATE block has no end-of-block code");
        return 0;
    }

    return huffman_build(literal_tree, literal_lengths,
                         DEFLATE_LITERAL_SYMBOLS, 0, error, error_size) &&
           huffman_build(distance_tree, distance_lengths,
                         DEFLATE_DISTANCE_SYMBOLS, 1, error, error_size);
}

static int inflate_compressed_block(BitReader *reader,
                                    InflateOutput *output,
                                    const HuffmanTree *literal_tree,
                                    const HuffmanTree *distance_tree,
                                    char *error, size_t error_size)
{
    for (;;)
    {
        uint16_t symbol;
        if (!huffman_decode(reader, literal_tree, &symbol,
                            error, error_size))
        {
            return 0;
        }

        if (symbol < 256U)
        {
            if (!output_emit(output, (uint8_t)symbol, error, error_size))
                return 0;
            continue;
        }
        if (symbol == 256U) return 1;
        if (symbol > 285U)
        {
            set_error(error, error_size, "invalid DEFLATE length symbol");
            return 0;
        }

        size_t length_index = symbol - 257U;
        uint32_t extra = 0;
        if (!bit_reader_read(reader, length_extra_bits[length_index],
                             &extra, error, error_size))
        {
            return 0;
        }
        uint32_t length = length_bases[length_index] + extra;

        uint16_t distance_symbol;
        if (!huffman_decode(reader, distance_tree, &distance_symbol,
                            error, error_size))
        {
            return 0;
        }
        if (distance_symbol >= 30U)
        {
            set_error(error, error_size, "invalid DEFLATE distance symbol");
            return 0;
        }

        extra = 0;
        if (!bit_reader_read(reader, distance_extra_bits[distance_symbol],
                             &extra, error, error_size))
        {
            return 0;
        }
        uint32_t distance = distance_bases[distance_symbol] + extra;
        if (!output_copy(output, distance, length, error, error_size)) return 0;
    }
}

static int inflate_stored_block(BitReader *reader,
                                InflateOutput *output,
                                char *error, size_t error_size)
{
    bit_reader_align_byte(reader);

    uint32_t length;
    uint32_t inverted_length;
    if (!bit_reader_read(reader, 16, &length, error, error_size) ||
        !bit_reader_read(reader, 16, &inverted_length, error, error_size))
    {
        return 0;
    }
    if ((length ^ 0xFFFFU) != inverted_length)
    {
        set_error(error, error_size, "invalid DEFLATE stored-block length");
        return 0;
    }

    for (uint32_t index = 0; index < length; index++)
    {
        uint32_t byte;
        if (!bit_reader_read(reader, 8, &byte, error, error_size) ||
            !output_emit(output, (uint8_t)byte, error, error_size))
        {
            return 0;
        }
    }
    return 1;
}

static int inflate_deflate_stream(BitReader *reader,
                                  InflateOutput *output,
                                  uint32_t *block_count,
                                  uint32_t *compressed_blocks,
                                  uint32_t *stored_blocks,
                                  char *error, size_t error_size)
{
    int final_block = 0;
    while (!final_block)
    {
        uint32_t value;
        if (!bit_reader_read(reader, 1, &value, error, error_size)) return 0;
        final_block = value != 0;
        if (!bit_reader_read(reader, 2, &value, error, error_size)) return 0;

        if (*block_count == UINT32_MAX)
        {
            set_error(error, error_size, "too many DEFLATE blocks");
            return 0;
        }
        (*block_count)++;

        if (value == 0U)
        {
            if (!inflate_stored_block(reader, output, error, error_size))
                return 0;
            (*stored_blocks)++;
        }
        else if (value == 1U || value == 2U)
        {
            HuffmanTree literal_tree;
            HuffmanTree distance_tree;
            int trees_ready = value == 1U
                ? build_fixed_trees(&literal_tree, &distance_tree,
                                    error, error_size)
                : build_dynamic_trees(reader, &literal_tree, &distance_tree,
                                      error, error_size);
            if (!trees_ready ||
                !inflate_compressed_block(reader, output,
                                          &literal_tree, &distance_tree,
                                          error, error_size))
            {
                return 0;
            }
            (*compressed_blocks)++;
        }
        else
        {
            set_error(error, error_size, "reserved DEFLATE block type");
            return 0;
        }

        if (!report_progress(output->options, output->input->bytes_read,
                             output->input_size, error, error_size))
        {
            return 0;
        }
    }

    bit_reader_align_byte(reader);
    return 1;
}

int gzip_decompress_file(const char *input_path, const char *output_path,
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

    FILE *input_file = fopen(input_path, "rb");
    if (input_file == NULL)
    {
        set_system_error(error, error_size, "cannot open input file");
        return 0;
    }

    GzipInput input = {input_file, 0};
    uint64_t input_size = get_path_size(input_path);
    if (!read_gzip_header(&input, error, error_size))
    {
        fclose(input_file);
        return 0;
    }
    if (!report_progress(options, input.bytes_read, input_size,
                         error, error_size))
    {
        fclose(input_file);
        return 0;
    }

    FILE *output_file = fopen(output_path, "wb");
    if (output_file == NULL)
    {
        set_system_error(error, error_size, "cannot open output file");
        fclose(input_file);
        return 0;
    }

    InflateOutput *output = calloc(1, sizeof(*output));
    if (output == NULL)
    {
        set_error(error, error_size, "out of memory for GZIP decoder");
        fclose(input_file);
        fclose(output_file);
        remove(output_path);
        return 0;
    }
    output->file = output_file;
    output->input = &input;
    output->options = options;
    output->input_size = input_size;
    output->crc = crc32_begin();

    BitReader reader = {&input, 0, 0};
    uint32_t block_count = 0;
    uint32_t compressed_blocks = 0;
    uint32_t stored_blocks = 0;
    int success = 0;

    if (!inflate_deflate_stream(&reader, output,
                                &block_count, &compressed_blocks,
                                &stored_blocks, error, error_size) ||
        !output_flush(output, error, error_size))
    {
        goto cleanup;
    }

    uint32_t expected_crc;
    uint32_t expected_size;
    if (!input_read_u32_le(&input, &expected_crc, error, error_size) ||
        !input_read_u32_le(&input, &expected_size, error, error_size))
    {
        goto cleanup;
    }

    uint32_t actual_crc = crc32_finish(output->crc);
    if (actual_crc != expected_crc)
    {
        set_error(error, error_size, "GZIP CRC32 mismatch: archive is corrupted");
        goto cleanup;
    }
    if ((uint32_t)(output->output_size & 0xFFFFFFFFU) != expected_size)
    {
        set_error(error, error_size, "GZIP output size does not match trailer");
        goto cleanup;
    }

    int trailing = fgetc(input_file);
    if (trailing != EOF)
    {
        set_error(error, error_size,
                  "additional GZIP members or trailing data are not supported");
        goto cleanup;
    }
    if (ferror(input_file))
    {
        set_system_error(error, error_size, "cannot finish reading GZIP file");
        goto cleanup;
    }
    if (fflush(output_file) != 0)
    {
        set_system_error(error, error_size, "cannot finalize output file");
        goto cleanup;
    }
    if (!report_progress(options, input_size, input_size,
                         error, error_size))
    {
        goto cleanup;
    }

    success = 1;
    if (stats != NULL)
    {
        stats->input_size = input.bytes_read;
        stats->output_size = output->output_size;
        stats->block_count = block_count;
        stats->compressed_blocks = compressed_blocks;
        stats->stored_blocks = stored_blocks;
    }

cleanup:
    free(output);
    if (fclose(input_file) != 0 && success)
    {
        set_system_error(error, error_size, "cannot close input file");
        success = 0;
    }
    if (fclose(output_file) != 0 && success)
    {
        set_system_error(error, error_size, "cannot close output file");
        success = 0;
    }
    if (!success) remove(output_path);
    return success;
}
