#include "checksum.h"

uint32_t crc32_begin(void)
{
    return UINT32_C(0xFFFFFFFF);
}

uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t size)
{
    for (size_t i = 0; i < size; i++)
    {
        crc ^= data[i];

        for (unsigned int bit = 0; bit < 8; bit++)
        {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1U);
            crc = (crc >> 1U) ^ (UINT32_C(0xEDB88320) & mask);
        }
    }

    return crc;
}

uint32_t crc32_finish(uint32_t crc)
{
    return crc ^ UINT32_C(0xFFFFFFFF);
}
