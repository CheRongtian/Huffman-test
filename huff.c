#include "huff.h"

#include "bitstream.h"

#include <stdlib.h>
#include <string.h>

#define HUFFMAN_MAX_NODES (HUFFMAN_SYMBOLS * 2U - 1U)

typedef struct
{
    uint64_t frequency;
    int left;
    int right;
    int symbol;
    unsigned int minimum_symbol;
} HuffmanNode;

typedef struct
{
    int items[HUFFMAN_MAX_NODES];
    size_t size;
    HuffmanNode *nodes;
} MinHeap;

typedef struct
{
    uint8_t length;
    uint8_t bits[HUFFMAN_MAX_CODE_BITS];
} CanonicalCode;

typedef struct
{
    int child[2];
    int symbol;
} DecodeNode;

static int node_less(const HuffmanNode *nodes, int lhs, int rhs)
{
    if (nodes[lhs].frequency != nodes[rhs].frequency)
    {
        return nodes[lhs].frequency < nodes[rhs].frequency;
    }
    if (nodes[lhs].minimum_symbol != nodes[rhs].minimum_symbol)
    {
        return nodes[lhs].minimum_symbol < nodes[rhs].minimum_symbol;
    }
    return lhs < rhs;
}

static void heap_push(MinHeap *heap, int node_index)
{
    size_t index = heap->size++;
    heap->items[index] = node_index;

    while (index > 0)
    {
        size_t parent = (index - 1U) / 2U;
        if (!node_less(heap->nodes, heap->items[index], heap->items[parent])) break;

        int temporary = heap->items[index];
        heap->items[index] = heap->items[parent];
        heap->items[parent] = temporary;
        index = parent;
    }
}

static int heap_pop(MinHeap *heap)
{
    int result = heap->items[0];
    heap->size--;

    if (heap->size == 0) return result;

    heap->items[0] = heap->items[heap->size];
    size_t index = 0;

    for (;;)
    {
        size_t left = index * 2U + 1U;
        size_t right = left + 1U;
        size_t smallest = index;

        if (left < heap->size &&
            node_less(heap->nodes, heap->items[left], heap->items[smallest]))
        {
            smallest = left;
        }
        if (right < heap->size &&
            node_less(heap->nodes, heap->items[right], heap->items[smallest]))
        {
            smallest = right;
        }
        if (smallest == index) break;

        int temporary = heap->items[index];
        heap->items[index] = heap->items[smallest];
        heap->items[smallest] = temporary;
        index = smallest;
    }

    return result;
}

static int assign_lengths(const HuffmanNode *nodes, int node_index,
                          unsigned int depth,
                          uint8_t lengths[HUFFMAN_SYMBOLS])
{
    const HuffmanNode *node = &nodes[node_index];

    if (node->symbol >= 0)
    {
        if (depth == 0 || depth > HUFFMAN_MAX_CODE_BITS) return 0;
        lengths[(unsigned int)node->symbol] = (uint8_t)depth;
        return 1;
    }

    if (depth >= HUFFMAN_MAX_CODE_BITS) return 0;
    return assign_lengths(nodes, node->left, depth + 1U, lengths) &&
           assign_lengths(nodes, node->right, depth + 1U, lengths);
}

static int build_code_lengths(const uint8_t *input, size_t input_size,
                              uint8_t lengths[HUFFMAN_SYMBOLS])
{
    uint64_t frequencies[HUFFMAN_SYMBOLS] = {0};
    HuffmanNode nodes[HUFFMAN_MAX_NODES];
    MinHeap heap = {{0}, 0, nodes};
    size_t node_count = 0;

    memset(lengths, 0, HUFFMAN_SYMBOLS);

    for (size_t i = 0; i < input_size; i++)
    {
        if (frequencies[input[i]] == UINT64_MAX) return 0;
        frequencies[input[i]]++;
    }

    for (unsigned int symbol = 0; symbol < HUFFMAN_SYMBOLS; symbol++)
    {
        if (frequencies[symbol] == 0) continue;

        nodes[node_count].frequency = frequencies[symbol];
        nodes[node_count].left = -1;
        nodes[node_count].right = -1;
        nodes[node_count].symbol = (int)symbol;
        nodes[node_count].minimum_symbol = symbol;
        heap_push(&heap, (int)node_count);
        node_count++;
    }

    if (heap.size == 0) return 0;

    if (heap.size == 1)
    {
        int only = heap_pop(&heap);
        lengths[(unsigned int)nodes[only].symbol] = 1;
        return 1;
    }

    while (heap.size > 1)
    {
        int left = heap_pop(&heap);
        int right = heap_pop(&heap);

        if (node_count >= HUFFMAN_MAX_NODES ||
            UINT64_MAX - nodes[left].frequency < nodes[right].frequency)
        {
            return 0;
        }

        nodes[node_count].frequency =
            nodes[left].frequency + nodes[right].frequency;
        nodes[node_count].left = left;
        nodes[node_count].right = right;
        nodes[node_count].symbol = -1;
        nodes[node_count].minimum_symbol =
            nodes[left].minimum_symbol < nodes[right].minimum_symbol
                ? nodes[left].minimum_symbol
                : nodes[right].minimum_symbol;
        heap_push(&heap, (int)node_count);
        node_count++;
    }

    return assign_lengths(nodes, heap_pop(&heap), 0, lengths);
}

static int increment_code(uint8_t bits[HUFFMAN_MAX_CODE_BITS],
                          unsigned int length)
{
    for (unsigned int position = length; position > 0; position--)
    {
        unsigned int index = position - 1U;
        if (bits[index] == 0)
        {
            bits[index] = 1;
            return 1;
        }
        bits[index] = 0;
    }

    return 0;
}

static int build_canonical_codes(
    const uint8_t lengths[HUFFMAN_SYMBOLS],
    CanonicalCode codes[HUFFMAN_SYMBOLS])
{
    unsigned int order[HUFFMAN_SYMBOLS];
    size_t count = 0;

    memset(codes, 0, sizeof(CanonicalCode) * HUFFMAN_SYMBOLS);

    for (unsigned int symbol = 0; symbol < HUFFMAN_SYMBOLS; symbol++)
    {
        if (lengths[symbol] != 0) order[count++] = symbol;
    }

    if (count == 0) return 0;

    for (size_t i = 1; i < count; i++)
    {
        unsigned int symbol = order[i];
        size_t position = i;

        while (position > 0)
        {
            unsigned int previous = order[position - 1U];
            if (lengths[previous] < lengths[symbol] ||
                (lengths[previous] == lengths[symbol] && previous < symbol))
            {
                break;
            }
            order[position] = previous;
            position--;
        }
        order[position] = symbol;
    }

    uint8_t current[HUFFMAN_MAX_CODE_BITS] = {0};
    unsigned int previous_length = lengths[order[0]];
    codes[order[0]].length = (uint8_t)previous_length;
    memcpy(codes[order[0]].bits, current, previous_length);

    for (size_t i = 1; i < count; i++)
    {
        unsigned int symbol = order[i];
        unsigned int length = lengths[symbol];

        if (length < previous_length ||
            !increment_code(current, previous_length))
        {
            return 0;
        }

        for (unsigned int bit = previous_length; bit < length; bit++)
        {
            current[bit] = 0;
        }

        codes[symbol].length = (uint8_t)length;
        memcpy(codes[symbol].bits, current, length);
        previous_length = length;
    }

    return 1;
}

static int build_decode_tree(const CanonicalCode codes[HUFFMAN_SYMBOLS],
                             DecodeNode nodes[HUFFMAN_MAX_NODES])
{
    for (size_t i = 0; i < HUFFMAN_MAX_NODES; i++)
    {
        nodes[i].child[0] = -1;
        nodes[i].child[1] = -1;
        nodes[i].symbol = -1;
    }

    size_t node_count = 1;

    for (unsigned int symbol = 0; symbol < HUFFMAN_SYMBOLS; symbol++)
    {
        if (codes[symbol].length == 0) continue;

        int current = 0;
        for (unsigned int bit_index = 0;
             bit_index < codes[symbol].length;
             bit_index++)
        {
            if (nodes[current].symbol >= 0) return 0;

            unsigned int bit = codes[symbol].bits[bit_index];
            int next = nodes[current].child[bit];

            if (next < 0)
            {
                if (node_count >= HUFFMAN_MAX_NODES) return 0;
                next = (int)node_count++;
                nodes[current].child[bit] = next;
            }
            current = next;
        }

        if (nodes[current].symbol >= 0 || nodes[current].child[0] >= 0 ||
            nodes[current].child[1] >= 0)
        {
            return 0;
        }
        nodes[current].symbol = (int)symbol;
    }

    return 1;
}

int huffman_compress(const uint8_t *input, size_t input_size,
                     uint8_t code_lengths[HUFFMAN_SYMBOLS],
                     uint8_t **output, size_t *output_size,
                     uint64_t *output_bit_count)
{
    if (input == NULL || input_size == 0 || code_lengths == NULL ||
        output == NULL || output_size == NULL || output_bit_count == NULL)
    {
        return 0;
    }

    *output = NULL;
    *output_size = 0;
    *output_bit_count = 0;

    if (!build_code_lengths(input, input_size, code_lengths)) return 0;

    CanonicalCode codes[HUFFMAN_SYMBOLS];
    if (!build_canonical_codes(code_lengths, codes)) return 0;

    BitWriter writer;
    bit_writer_init(&writer);

    for (size_t i = 0; i < input_size; i++)
    {
        const CanonicalCode *code = &codes[input[i]];
        for (unsigned int bit = 0; bit < code->length; bit++)
        {
            if (!bit_writer_write(&writer, code->bits[bit]))
            {
                bit_writer_free(&writer);
                return 0;
            }
        }
    }

    *output = writer.data;
    *output_size = writer.size;
    *output_bit_count = writer.bit_count;
    return 1;
}

int huffman_decompress(const uint8_t *input, size_t input_size,
                       uint64_t input_bit_count,
                       const uint8_t code_lengths[HUFFMAN_SYMBOLS],
                       uint8_t *output, size_t output_size)
{
    if (input == NULL || input_size == 0 || input_bit_count == 0 ||
        code_lengths == NULL || output == NULL || output_size == 0)
    {
        return 0;
    }

    CanonicalCode codes[HUFFMAN_SYMBOLS];
    if (!build_canonical_codes(code_lengths, codes)) return 0;

    DecodeNode nodes[HUFFMAN_MAX_NODES];
    if (!build_decode_tree(codes, nodes)) return 0;

    BitReader reader;
    if (!bit_reader_init(&reader, input, input_size, input_bit_count)) return 0;

    size_t used = 0;
    int current = 0;

    while (!bit_reader_finished(&reader))
    {
        unsigned int bit;
        if (!bit_reader_read(&reader, &bit)) return 0;

        current = nodes[current].child[bit];
        if (current < 0) return 0;

        if (nodes[current].symbol >= 0)
        {
            if (used >= output_size) return 0;
            output[used++] = (uint8_t)nodes[current].symbol;
            current = 0;
        }
    }

    return current == 0 && used == output_size;
}
