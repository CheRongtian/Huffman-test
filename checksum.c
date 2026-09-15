#include "checksum.h"
#include <sys/types.h>

u_short cksum(u_short *buf, int count) // Checksum process
{
    register u_long sum = 0;
    
    while(count --)
    {
        sum += *buf++;
        if(sum & 0xFFFF0000) // if upper 16 bit AND sum == 1
        {
            /* carry occurred, so wrap around */
            sum &= 0xFFFF; // sum AND 16-bit 1
            sum ++; // wrap around
        }
    }
    return ~(sum & 0xFFFF);
}