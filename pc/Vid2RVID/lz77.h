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

#ifdef __cplusplus
extern "C"
{
#endif

const char* lzssCompress(const char* Data);

#ifdef __cplusplus
}
#endif

#endif // LZSS_H_

