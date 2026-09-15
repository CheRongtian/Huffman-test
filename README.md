# HuffmanTree / MGZ

An educational block-based compression tool with a C11 codec, a command-line
client, and an optional Qt 6 Widgets desktop application. It combines LZ77,
Canonical Huffman coding, bitstream packing, and CRC32 in a custom `.mgz`
file format.

## Quick start

On macOS or Linux, build and launch the desktop application from the project
root with:

```sh
./run.sh
```

The script locates Qt 6 when it is installed through Homebrew or available on
the system, configures the `build` directory, builds the `mgz_gui` target, and
launches MGZ Compressor. On macOS, install missing Qt 6 development files with
`brew install qt`, then run the script again.

## Build

```sh
cmake -S . -B build
cmake --build build
```

The command-line target only requires a C/C++ compiler and CMake. When the Qt 6
Widgets development package is available, CMake also builds the desktop target.
If Qt is installed in a nonstandard location, pass its CMake prefix explicitly:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/platform
cmake --build build
```

## Usage

```sh
./build/huff -c input.bin output.mgz
./build/huff -d output.mgz restored.bin
```

The program supports text, images, and other binary files. Input is processed in
independent 64 KiB blocks, so memory usage remains bounded as file size grows.
When LZ77 and Huffman coding do not make a block smaller, the block is stored
without compression.

## Desktop application

The `mgz_gui` target provides a local desktop interface with:

- Drag-and-drop and keyboard-accessible file selection.
- Compression and extraction modes.
- Batch task processing with per-file progress and cancellation.
- Same-folder or selected-folder output.
- A configurable archive suffix, with `.mgz` as the default.
- Output conflict handling and completion statistics.
- A lightweight lightning flash when a compression or extraction batch
  completes successfully.
- Light and dark appearance derived from the system palette.

On macOS, the application bundle can be opened after a Qt-enabled build with:

```sh
open "build/MGZ Compressor.app"
```

## Modules

- `bitstream.c/.h`: Reads and writes individual bits in MSB-first order while
  preserving the exact bit count.
- `huff.c/.h`: Counts frequencies, generates code lengths, builds Canonical
  Huffman codes, and performs Huffman encoding and decoding.
- `lz77.c/.h`: Implements a 32 KiB sliding window, 3-to-258-byte matches, and
  hash-chain match lookup.
- `checksum.c/.h`: Implements incrementally updatable IEEE CRC-32.
- `format.c/.h`: Implements the MGZ format, block I/O, malformed-input
  validation, progress/cancellation callbacks, and the compression pipeline.
- `main.c`: Handles command-line arguments, statistics, and error reporting.
- `gui/`: Implements the Qt 6 Widgets desktop interface and background task
  worker, including the non-blocking completion lightning overlay.
- `flash.html`: Preserves the original browser-based lightning simulation as a
  visual reference; the desktop application uses a lightweight native Qt
  implementation at runtime.

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
