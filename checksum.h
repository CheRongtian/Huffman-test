#ifndef CHECKSUM_H
#define CHECKSUM_H

#include <stddef.h>
#include <stdint.h>

uint32_t crc32_begin(void);
uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t size);
uint32_t crc32_finish(uint32_t crc);

#endif
