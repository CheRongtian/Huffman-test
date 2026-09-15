# HuffmanTree / MGZ

An educational block-based compression tool written in C11. It combines LZ77,
Canonical Huffman coding, bitstream packing, and CRC32 in a custom `.mgz` file
format.

## Build

```sh
cmake -S . -B build
cmake --build build
```

You can also run `./test.sh` to build `huff` in the current directory with
Clang.

## Usage

```sh
./huff -c input.bin output.mgz
./huff -d output.mgz restored.bin
```

The program supports text, images, and other binary files. Input is processed in
independent 64 KiB blocks, so memory usage remains bounded as file size grows.
When LZ77 and Huffman coding do not make a block smaller, the block is stored
without compression.

## Modules

- `bitstream.c/.h`: Reads and writes individual bits in MSB-first order while
  preserving the exact bit count.
- `huff.c/.h`: Counts frequencies, generates code lengths, builds Canonical
  Huffman codes, and performs Huffman encoding and decoding.
- `lz77.c/.h`: Implements a 32 KiB sliding window, 3-to-258-byte matches, and
  hash-chain match lookup.
- `checksum.c/.h`: Implements incrementally updatable IEEE CRC-32.
- `format.c/.h`: Implements the MGZ format, block I/O, malformed-input
  validation, and the compression pipeline.
- `main.c`: Handles command-line arguments, statistics, and error reporting.

## MGZ v1 Format

All multibyte integers use little-endian byte order.

### File header (32 bytes)

| Field | Size | Description |
| --- | ---: | --- |
| Magic | 4 | `MGZ1` |
| Version | 1 | Currently 1 |
| Flags | 1 | Currently 0 |
| Header size | 2 | 32 |
| Block size | 4 | 65536 |
| Original size | 8 | Original file size in bytes |
| CRC32 | 4 | IEEE CRC-32 of the original file |
| Block count | 4 | Number of data blocks |
| Reserved | 4 | Currently 0 |

### Block header (12 bytes)

| Field | Size | Description |
| --- | ---: | --- |
| Method | 1 | 0 for stored data, 1 for LZ77 + Huffman |
| Reserved | 3 | Currently 0 |
| Original block size | 4 | Uncompressed block size |
| Payload size | 4 | Size of the payload that follows |

The payload of a compressed block contains, in order, a 4-byte LZ77 token-stream
size, an 8-byte valid-bit count, a 256-byte Huffman code-length table, and the
packed bitstream. The decoder validates the file header, block sizes, Canonical
Huffman table, LZ77 distances and lengths, trailing padding bits, original size,
and whole-file CRC32.

`.mgz` is a custom educational format and is not interoperable with standard
gzip files.
