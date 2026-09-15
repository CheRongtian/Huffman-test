#ifndef CHECKSUM_H
#define CHECKSUM_H
#include <sys/types.h>

u_short cksum(u_short *buf, int count);

#endif