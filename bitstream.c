#include "bitstream.h"

#include <stdlib.h>

static int bit_writer_reserve(BitWriter *writer, size_t needed)
{
    if (needed <= writer->capacity) return 1;

    size_t capacity = writer->capacity == 0 ? 64U : writer->capacity;

    while (capacity < needed)
    {
        if (capacity > SIZE_MAX / 2U)
        {
            capacity = needed;
            break;
        }
        capacity *= 2U;
    }

    uint8_t *grown = realloc(writer->data, capacity);
    if (grown == NULL)
    {
        writer->failed = 1;
        return 0;
    }

    writer->data = grown;
    writer->capacity = capacity;
    return 1;
}

void bit_writer_init(BitWriter *writer)
{
    writer->data = NULL;
    writer->size = 0;
    writer->capacity = 0;
    writer->bit_count = 0;
    writer->failed = 0;
}

void bit_writer_free(BitWriter *writer)
{
    free(writer->data);
    bit_writer_init(writer);
}

int bit_writer_write(BitWriter *writer, unsigned int bit)
{
    if (writer->failed || bit > 1U || writer->bit_count == UINT64_MAX)
    {
        writer->failed = 1;
        return 0;
    }

    if ((writer->bit_count & 7U) == 0U)
    {
        if (writer->size == SIZE_MAX ||
            !bit_writer_reserve(writer, writer->size + 1U))
        {
            writer->failed = 1;
            return 0;
        }
        writer->data[writer->size++] = 0;
    }

    unsigned int shift = 7U - (unsigned int)(writer->bit_count & 7U);
    writer->data[writer->size - 1U] |= (uint8_t)(bit << shift);
    writer->bit_count++;
    return 1;
}

int bit_reader_init(BitReader *reader, const uint8_t *data, size_t size,
                    uint64_t bit_count)
{
    if (size > UINT64_MAX / 8U || bit_count > (uint64_t)size * 8U)
    {
        return 0;
    }

    if (size > 0 && data == NULL) return 0;

    reader->data = data;
    reader->size = size;
    reader->bit_count = bit_count;
    reader->position = 0;
    return 1;
}

int bit_reader_read(BitReader *reader, unsigned int *bit)
{
    if (bit == NULL || reader->position >= reader->bit_count) return 0;

    size_t byte_index = (size_t)(reader->position / 8U);
    unsigned int shift = 7U - (unsigned int)(reader->position & 7U);
    *bit = (reader->data[byte_index] >> shift) & 1U;
    reader->position++;
    return 1;
}

int bit_reader_finished(const BitReader *reader)
{
    return reader->position == reader->bit_count;
}
