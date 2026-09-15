#include "lz77.h"

#include <stdint.h>
#include <stdlib.h>

#define LZ77_HASH_SIZE 65536U
#define LZ77_MAX_CHAIN 128U
#define TOKEN_LITERAL 0U
#define TOKEN_MATCH 1U

static unsigned int hash_bytes(const uint8_t *data)
{
    uint32_t value = (uint32_t)data[0] * UINT32_C(251);
    value = (value + data[1]) * UINT32_C(251);
    value += data[2];
    return (unsigned int)(value & (LZ77_HASH_SIZE - 1U));
}

static void insert_position(const uint8_t *input, size_t input_size,
                            size_t position, int32_t *head, int32_t *previous)
{
    if (position + LZ77_MIN_MATCH > input_size) return;

    unsigned int hash = hash_bytes(input + position);
    previous[position] = head[hash];
    head[hash] = (int32_t)position;
}

int lz77_compress(const uint8_t *input, size_t input_size,
                  uint8_t **output, size_t *output_size)
{
    if ((input == NULL && input_size != 0) || output == NULL ||
        output_size == NULL || input_size > (size_t)INT32_MAX ||
        input_size > SIZE_MAX / 2U)
    {
        return 0;
    }

    *output = NULL;
    *output_size = 0;

    if (input_size == 0) return 1;

    size_t capacity = input_size * 2U;
    uint8_t *encoded = malloc(capacity);
    int32_t *head = malloc(sizeof(*head) * LZ77_HASH_SIZE);
    int32_t *previous = malloc(sizeof(*previous) * input_size);

    if (encoded == NULL || head == NULL || previous == NULL)
    {
        free(encoded);
        free(head);
        free(previous);
        return 0;
    }

    for (size_t i = 0; i < LZ77_HASH_SIZE; i++) head[i] = -1;

    size_t position = 0;
    size_t used = 0;

    while (position < input_size)
    {
        size_t best_length = 0;
        size_t best_distance = 0;

        if (position + LZ77_MIN_MATCH <= input_size)
        {
            unsigned int hash = hash_bytes(input + position);
            int32_t candidate = head[hash];
            unsigned int searched = 0;

            while (candidate >= 0 && searched < LZ77_MAX_CHAIN)
            {
                size_t candidate_position = (size_t)candidate;
                size_t distance = position - candidate_position;
                if (distance > LZ77_WINDOW_SIZE) break;

                size_t limit = input_size - position;
                if (limit > LZ77_MAX_MATCH) limit = LZ77_MAX_MATCH;

                size_t length = 0;
                while (length < limit &&
                       input[candidate_position + length] ==
                           input[position + length])
                {
                    length++;
                }

                if (length > best_length)
                {
                    best_length = length;
                    best_distance = distance;
                    if (best_length == limit) break;
                }

                candidate = previous[candidate_position];
                searched++;
            }
        }

        if (best_length >= LZ77_MIN_MATCH)
        {
            encoded[used++] = TOKEN_MATCH;
            encoded[used++] = (uint8_t)(best_length & 0xFFU);
            encoded[used++] = (uint8_t)((best_length >> 8U) & 0xFFU);
            encoded[used++] = (uint8_t)(best_distance & 0xFFU);
            encoded[used++] = (uint8_t)((best_distance >> 8U) & 0xFFU);

            for (size_t offset = 0; offset < best_length; offset++)
            {
                insert_position(input, input_size, position + offset,
                                head, previous);
            }
            position += best_length;
        }
        else
        {
            encoded[used++] = TOKEN_LITERAL;
            encoded[used++] = input[position];
            insert_position(input, input_size, position, head, previous);
            position++;
        }
    }

    free(head);
    free(previous);
    *output = encoded;
    *output_size = used;
    return 1;
}

int lz77_decompress(const uint8_t *input, size_t input_size,
                    uint8_t *output, size_t output_capacity,
                    size_t *output_size)
{
    if ((input == NULL && input_size != 0) ||
        (output == NULL && output_capacity != 0) || output_size == NULL)
    {
        return 0;
    }

    size_t input_position = 0;
    size_t used = 0;

    while (input_position < input_size)
    {
        uint8_t token = input[input_position++];

        if (token == TOKEN_LITERAL)
        {
            if (input_position >= input_size || used >= output_capacity) return 0;
            output[used++] = input[input_position++];
            continue;
        }

        if (token != TOKEN_MATCH || input_size - input_position < 4U) return 0;

        size_t length = (size_t)input[input_position] |
                        ((size_t)input[input_position + 1U] << 8U);
        size_t distance = (size_t)input[input_position + 2U] |
                          ((size_t)input[input_position + 3U] << 8U);
        input_position += 4U;

        if (length < LZ77_MIN_MATCH || length > LZ77_MAX_MATCH ||
            distance == 0 || distance > LZ77_WINDOW_SIZE || distance > used ||
            length > output_capacity - used)
        {
            return 0;
        }

        for (size_t i = 0; i < length; i++)
        {
            output[used] = output[used - distance];
            used++;
        }
    }

    *output_size = used;
    return 1;
}
