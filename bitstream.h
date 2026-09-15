#ifndef BITSTREAM_H
#define BITSTREAM_H

#include <stddef.h>
#include <stdint.h>

typedef struct
{
    uint8_t *data;
    size_t size;
    size_t capacity;
    uint64_t bit_count;
    int failed;
} BitWriter;

typedef struct
{
    const uint8_t *data;
    size_t size;
    uint64_t bit_count;
    uint64_t position;
} BitReader;

void bit_writer_init(BitWriter *writer);
void bit_writer_free(BitWriter *writer);
int bit_writer_write(BitWriter *writer, unsigned int bit);

int bit_reader_init(BitReader *reader, const uint8_t *data, size_t size,
                    uint64_t bit_count);
int bit_reader_read(BitReader *reader, unsigned int *bit);
int bit_reader_finished(const BitReader *reader);

#endif
