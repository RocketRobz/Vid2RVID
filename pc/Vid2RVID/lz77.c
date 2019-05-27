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

const char* lzssCompress(const char* Data)
{
			char* dataptr;

			char* result = (char*)(sizeof(Data) + sizeof(Data) / 8 + 4);
			char* resultptr;
			*resultptr++ = 0x10;
			*resultptr++ = (char)(sizeof(Data) & 0xFF);
			*resultptr++ = (char)((sizeof(Data) >> 8) & 0xFF);
			*resultptr++ = (char)((sizeof(Data) >> 16) & 0xFF);
			int length = sizeof(Data);
			int dstoffs = 4;
			int Offs = 0;
			while (1)
			{
				int headeroffs = dstoffs++;
				resultptr++;
				char header = 0;
				for (int i = 0; i < 8; i++)
				{
					int comp = 0;
					int back = 1;
					int nr = 2;
					{
						char* ptr = dataptr - 1;
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
						*resultptr++ = (char)((((back - 1) >> 8) & 0xF) | (((nr - 3) & 0xF) << 4));
						*resultptr++ = (char)((back - 1) & 0xFF);
						dstoffs += 2;
						comp = 1;
					}
					else
					{
						*resultptr++ = *dataptr++;
						dstoffs++;
						Offs++;
					}
					header = (char)((header << 1) | (comp & 1));
					if (Offs >= length)
					{
						header = (char)(header << (7 - i));
						break;
					}
				}
				result[headeroffs] = header;
				if (Offs >= length) break;
			}
			while ((dstoffs % 4) != 0) dstoffs++;
			const char* realresult = (const char*)(dstoffs);
			memcpy(result, realresult, dstoffs);
			return realresult;
}
