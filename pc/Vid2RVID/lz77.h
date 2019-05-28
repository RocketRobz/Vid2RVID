#ifndef LZSS_H_
#define LZSS_H_

#include <stdio.h>
#include <stdlib.h>         //built with codeblocks and mingw
#include <math.h>
#include <time.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>                       //probably don't need most of these :p
#include <stdint.h>
#include <string.h>

extern int compressedDataSize;

unsigned char *lzssCompress(unsigned char *Data, int dataSize);

#endif // LZSS_H_

