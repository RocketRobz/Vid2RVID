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
#include "tonccpy.h"

int compressedDataSize = 0;

unsigned char *lzssCompress(unsigned char *Data, int dataSize)
{
			unsigned char* dataptr = Data;

			unsigned char* result = new unsigned char[dataSize + dataSize / 8 + 4];
			unsigned char* resultptr = result;
			*resultptr++ = 0x10;
			*resultptr++ = (unsigned char)(dataSize & 0xFF);
			*resultptr++ = (unsigned char)((dataSize >> 8) & 0xFF);
			*resultptr++ = (unsigned char)((dataSize >> 16) & 0xFF);
			int length = dataSize;
			int dstoffs = 4;
			int Offs = 0;
			while (1)
			{
				int headeroffs = dstoffs++;
				resultptr++;
				unsigned char header = 0;
				for (int i = 0; i < 8; i++)
				{
					int comp = 0;
					int back = 1;
					int nr = 2;
					{
						unsigned char* ptr = dataptr - 1;
						int maxnum = 18;
						if (length - Offs < maxnum) maxnum = length - Offs;
						int maxback = 0x1000;
						if (Offs < maxback) maxback = Offs;
						maxback = (int)dataptr - maxback;
						int tmpnr;
						while (maxback <= (int)ptr)
						{
							if (*(unsigned short*)ptr == *(unsigned short*)dataptr && ptr[2] == dataptr[2])
							{
								tmpnr = 3;
								while (tmpnr < maxnum && ptr[tmpnr] == dataptr[tmpnr]) tmpnr++;
								if (tmpnr > nr)
								{
									if (Offs + tmpnr > length)
									{
										nr = length - Offs;
										back = (int)(dataptr - ptr);
										break;
									}
									nr = tmpnr;
									back = (int)(dataptr - ptr);
									if (nr == maxnum) break;
								}
							}
							--ptr;
						}
					}
					if (nr > 2)
					{
						Offs += nr;
						dataptr += nr;
						*resultptr++ = (unsigned char)((((back - 1) >> 8) & 0xF) | (((nr - 3) & 0xF) << 4));
						*resultptr++ = (unsigned char)((back - 1) & 0xFF);
						dstoffs += 2;
						comp = 1;
					}
					else
					{
						*resultptr++ = *dataptr++;
						dstoffs++;
						Offs++;
					}
					header = (unsigned char)((header << 1) | (comp & 1));
					if (Offs >= length)
					{
						header = (unsigned char)(header << (7 - i));
						break;
					}
				}
				result[headeroffs] = header;
				if (Offs >= length) break;
			}
			while ((dstoffs % 4) != 0) dstoffs++;
			compressedDataSize = dstoffs;
			unsigned char* realresult = new unsigned char[dstoffs];
			tonccpy(realresult, result, dstoffs);
			return realresult;
}
