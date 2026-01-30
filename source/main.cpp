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
#include <thread>
#ifdef _WIN32
#include <Windows.h>
#endif

#include "graphics/lodepng.h"
#include "lz77.h"
#include "sha1.h"
#include "inifile.h"

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;

template<class TYPE> inline TYPE BIT(const TYPE & x)
{ return TYPE(1) << x; }

void clear_screen() {
	#ifdef _WIN32
	char fill = ' ';
	COORD tl = {0,0};
	CONSOLE_SCREEN_BUFFER_INFO s;
	HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
	GetConsoleScreenBufferInfo(console, &s);
	DWORD written, cells = s.dwSize.X * s.dwSize.Y;
	FillConsoleOutputCharacter(console, fill, cells, tl, &written);
	FillConsoleOutputAttribute(console, s.wAttributes, cells, tl, &written);
	SetConsoleCursorPosition(console, tl);
	#else
	printf("\x1b[2J\x1b[1;1H");
	#endif
}

#ifndef _WIN32
void wait_any_key() {
	while ( getchar() != '\n' );
}
#endif

#define isGba 0
#define isNds 1

int gameConsole = 2;

static bool bottomField[8][2] = {{false}};
static int splitPointReached = 0;
static int previousFrame = 0;
static int lruCachePos = 0;
static int convertedFrames = 0;

bool paletteSet[8][256] = {{false}};
u16 palette[8][256] = {{0}};

u8 convertedFrame[8][256*192];
u16 convertedFrame16[8][256*192];
u8 halvedFrame[8][256*96];
u16 halvedFrame16[8][256*96];
char convertedFrameSHA1[20];
unsigned char* compressedFrame[8] = {NULL};

char fileBuffer[0x100000] = {0};
u32 frameOffsetTableSize = 0;
u32 frameOffset = 0;
u32 compressedFrameSizeTableSize = 0;
u64 tempFramesSize = 0;
u32 soundSize = 0;
u64 sizeCheck = 0;

int hRes = 0;
bool framesCompressed = false;
u32* compressedFrameSizeTable32 = NULL;
u16* compressedFrameSizeTable16 = NULL;

u8 headerToFile[0x200] = {0};

u32 getFileSize(const char *fileName)
{
	FILE* fp = fopen(fileName, "rb");
	u32 fsize = 0;
	if (fp) {
		fseek(fp, 0, SEEK_END);
		fsize = ftell(fp);			// Get source file's size
		fseek(fp, 0, SEEK_SET);
	}
	fclose(fp);

	return fsize;
}

#define rvidVer 5

typedef struct rvidHeaderInfo {
	u32 formatString;  	    // "RVID" string
	u32 ver;			    // File format version
	u32 frames;			    // Number of frames
	u8 fps;				    // Frames per second
	u8 vRes;			    // Vertical resolution
	u8 interlaced;		    // Is interlaced
	u8 dualScreen;		    // Is dual screen video, 2 = Video is for GBA
	u16 sampleRate;			// Audio sample rate
	u8 audioBitMode;		// 0 = 8-bit, 1 = 16-bit
	u8 bmpMode;        		// 0 = 8 BPP (RGB565), 1 = 16 BPP (RGB555), 2 = 16 BPP (RGB565)
	u32 compressedFrameSizeTableOffset;		// Offset of compressed frame size table
	u32 soundLeftOffset;		// Offset of left-side sound stream
	u32 soundRightOffset;		// Offset of right-side sound stream
} rvidHeaderInfo;

rvidHeaderInfo rvidHeader;
const char* framesFolder = "rvidFrames";

#ifdef _WIN32
#define titleText "Vid2RVID v1.7\nby Rocket Robz\n"
#else
#define titleText "Vid2RVID v1.7\nby Rocket Robz\nLinux support by Paulo Mateus\n"
#endif

/*void extractFrames(void) {
	clear_screen();
	printf("Extracting frames...\n");

	mkdir("rvidFrames_extracted");
	FILE* videoInput = fopen("source.rvid", "rb");
	chdir("rvidFrames_extracted");
	FILE* frameOutput;
	char frameOutputFileName[32];
	fread(&rvidHeader, 1, sizeof(rvidHeaderInfo), videoInput);
	fseek(videoInput, 0x200, SEEK_SET);
	for (int i = 0; i < rvidHeader.frames; i++) {
		if (fread(convertedFrame, 1, 0x200*rvidHeader.vRes, videoInput) > 0) {
			snprintf(frameOutputFileName, sizeof(frameOutputFileName), "frame%i.bin", i);
			frameOutput = fopen(frameOutputFileName, "wb");
			fwrite(convertedFrame, 1, 0x200*rvidHeader.vRes, frameOutput);
			fclose(frameOutput);
		} else {
			break;
		}
	}
	fclose(videoInput);
	printf("Done!\n");
}*/

void convertFrame(const int thread, const int b, const unsigned width, std::vector<unsigned char> image, bool alternatePixel) {
	if (rvidHeader.bmpMode) {
		memset(convertedFrame16[thread], 0, (256*192)*2);
	} else {
		for (int i = 0; i < 256; i++) {
			paletteSet[thread][i] = false;
			palette[thread][i] = 0;
		}
		memset(convertedFrame[thread], 0, 256*192);
	}

	const int screenWidth = (gameConsole == isGba) ? 240 : 256;

	int xPos = 0;
	if ((unsigned)width <= screenWidth-2) {
		// Adjust video positioning
		for (int i = (int)width; i < screenWidth; i += 2) {
			xPos++;
		}
	}
	int x = 0;
	int y = 0;
	for(unsigned i=0;i<image.size()/4;i++) {
		if (rvidHeader.bmpMode && alternatePixel) {
			if (image[(i*4)] >= 0x4 && image[(i*4)] < 0xFC) {
				image[(i*4)] += 0x4;
			}
			if (rvidHeader.bmpMode == 2) {
				if (image[(i*4)+1] >= 0x2 && image[(i*4)+1] < 0xFE) {
					image[(i*4)+1] += 0x2;
				}
			} else {
				if (image[(i*4)+1] >= 0x4 && image[(i*4)+1] < 0xFC) {
					image[(i*4)+1] += 0x4;
				}
			}
			if (image[(i*4)+2] >= 0x4 && image[(i*4)+2] < 0xFC) {
				image[(i*4)+2] += 0x4;
			}
		}

		u16 color = 0;
		if (rvidHeader.bmpMode == 1) {
			color = image[i*4]>>3 | (image[(i*4)+1]>>3)<<5 | (image[(i*4)+2]>>3)<<10 | BIT(15);
		} else {
			const u16 green = (image[(i*4)+1] >> 2) << 5;
			color = image[i*4] >> 3 | (image[(i*4)+2] >> 3) << 10;
			if (green & BIT(5)) {
				color |= BIT(15);
			}
			for (int gBit = 6; gBit <= 10; gBit++) {
				if (green & BIT(gBit)) {
					color |= BIT(gBit-1);
				}
			}
		}

		if (rvidHeader.bmpMode) {
			convertedFrame16[thread][(y * screenWidth) + xPos + x] = color;

			x++;
			if ((unsigned)x == width) {
				if ((x % 2) == 0) alternatePixel = !alternatePixel;
				x=0;
				y++;
			}
			alternatePixel = !alternatePixel;
		} else {
			int p = 0;
			for (p = 0; p < 256; p++) {
				if (!paletteSet[thread][p]) {
					palette[thread][p] = color;
					paletteSet[thread][p] = true;
					break;
				} else if (palette[thread][p] == color) {
					break;
				}
			}
			convertedFrame[thread][(y * screenWidth) + xPos + x] = p;

			x++;
			if ((unsigned)x == width) {
				x=0;
				y++;
			}
		}
	}

	if (rvidHeader.bmpMode) {
		if (rvidHeader.interlaced) {
			int f = bottomField[thread][b] ? 1 : 0;
			int x = 0;
			for(int i = 0; i < screenWidth*rvidHeader.vRes; i++) {
				halvedFrame16[thread][i] = convertedFrame16[thread][(screenWidth*f)+x];
				x++;
				if (x == screenWidth) {
					f += 2;
					x = 0;
				}
			}
			bottomField[thread][b] = !bottomField[thread][b];
		}
	} else {
		if (rvidHeader.interlaced) {
			int f = bottomField[thread][b] ? 1 : 0;
			int x = 0;
			for(int i = 0; i < screenWidth*rvidHeader.vRes; i++) {
				halvedFrame[thread][i] = convertedFrame[thread][(screenWidth*f)+x];
				x++;
				if (x == screenWidth) {
					f += 2;
					x = 0;
				}
			}

			bottomField[thread][b] = !bottomField[thread][b];
		}
	}
}

static inline void getFrameChecksum(const u32 frameFileSize) {
	SHA1(convertedFrameSHA1, (char*)convertedFrame16[0], frameFileSize);
}

int jobsDone = 0;

void applyRgb565Dither(const int firstFrame, const int lastFrame) {
	char framePath[256];
	for (int i = firstFrame; i < lastFrame; i++) {
		sprintf(framePath, "%s/frame%i.png", framesFolder, i);
		if (access(framePath, F_OK) != 0) break;
		for (int b = 0; b < rvidHeader.dualScreen+1; b++) {
			std::vector<unsigned char> image;
			unsigned width, height;
			lodepng::decode(image, width, height, framePath);

			if (b == 0) convertedFrames++;

			bool alternatePixel = !rvidHeader.interlaced && (i % 2);
			int x = 0;
			for(unsigned i=0;i<image.size();i+=4) {
				if (alternatePixel) {
					if (image[i] >= 0x4 && image[i] < 0xFC) {
						image[i] += 0x4;
					}
					if (image[i+1] >= 0x2 && image[i+1] < 0xFE) {
						image[i+1] += 0x2;
					}
					if (image[i+2] >= 0x4 && image[i+2] < 0xFC) {
						image[i+2] += 0x4;
					}
				}

				const u8 r = image[i] >> 3;
				const u8 g = image[i+1] >> 2;
				const u8 b = image[i+2] >> 3;

				image[i] = (r * 255) / 31;
				image[i+1] = (g * 255) / 63;
				image[i+2] = (b * 255) / 31;

				x++;
				if ((unsigned)x == width) {
					if ((x % 2) == 0) alternatePixel = !alternatePixel;
					x=0;
				}
				alternatePixel = !alternatePixel;
			}
			lodepng::encode(framePath, image, width, height);

			if ((b == 0) && rvidHeader.dualScreen) {
				sprintf(framePath, "%s/bottom/frame%i.png", framesFolder, i);
			}
		}
	}

	jobsDone++;
}

void convertAndWriteFrames(FILE* tempFrames, const int thread, const int firstFrame, const int lastFrame, u32* frameOffsetTableWithDupes) {
	char framePath[256];
	int prevFrameSize = 0;
	int i2 = 0;
	for (int i = firstFrame; i < lastFrame; i++) {
		sprintf(framePath, "%s/frame%i.png", framesFolder, i);
		if (access(framePath, F_OK) != 0) break;
		for (int b = 0; b < rvidHeader.dualScreen+1; b++) {
			const int num = rvidHeader.dualScreen ? (i*2)+b : i;

			std::vector<unsigned char> image;
			unsigned width, height;
			lodepng::decode(image, width, height, framePath);

			if (b == 0) convertedFrames++;

			convertFrame(thread, b, width, image, !rvidHeader.interlaced && (i % 2));

			// Save current frame to temp file
			if (!rvidHeader.bmpMode) {
				fwrite(palette[thread], 2, 256, tempFrames);
			}

			int frameFileSize = 0;
			if (framesCompressed) {
				if (rvidHeader.bmpMode) {
					if (rvidHeader.interlaced) {
						compressedFrame[thread] = lzssCompress(&frameFileSize, (unsigned char*)halvedFrame16[thread], 0x200*rvidHeader.vRes);
					} else {
						compressedFrame[thread] = lzssCompress(&frameFileSize, (unsigned char*)convertedFrame16[thread], 0x200*rvidHeader.vRes);
					}
				} else {
					if (rvidHeader.interlaced) {
						compressedFrame[thread] = lzssCompress(&frameFileSize, (unsigned char*)halvedFrame[thread], 0x100*rvidHeader.vRes);
					} else {
						compressedFrame[thread] = lzssCompress(&frameFileSize, (unsigned char*)convertedFrame[thread], 0x100*rvidHeader.vRes);
					}
				}
			}

			if (!framesCompressed || (frameFileSize >= hRes*rvidHeader.vRes)) {
				// Store uncompressed frame if compressed frame is exactly the same size or larger, or if compression is disabled
				frameFileSize = hRes*rvidHeader.vRes;
				if (rvidHeader.bmpMode) {
					fwrite(rvidHeader.interlaced ? halvedFrame16[thread] : convertedFrame16[thread], 1, frameFileSize, tempFrames);
				} else {
					fwrite(rvidHeader.interlaced ? halvedFrame[thread] : convertedFrame[thread], 1, frameFileSize, tempFrames);
				}
			} else {
				fwrite(compressedFrame[thread], 1, frameFileSize, tempFrames);
			}

			if (framesCompressed) {
				if (rvidHeader.bmpMode) {
					compressedFrameSizeTable32[num] = frameFileSize;
				} else {
					compressedFrameSizeTable16[num] = frameFileSize;
				}
				delete[] compressedFrame[thread];
			}

			const int num2 = rvidHeader.dualScreen ? (i2*2)+b : i2;
			if (num2 > 0) {
				frameOffsetTableWithDupes[num] = frameOffsetTableWithDupes[num-1] + prevFrameSize;
			}
			if (!rvidHeader.bmpMode) {
				frameFileSize += 0x200;
			}
			prevFrameSize = frameFileSize;

			if ((b == 0) && rvidHeader.dualScreen) {
				sprintf(framePath, "%s/bottom/frame%i.png", framesFolder, i);
			}
		}
		i2++;
	}

	jobsDone++;
}

int main(int argc, char **argv) {

	int selector = 0;

	printf(titleText);
	printf("\n");
	printf("Path of video frames:\n\"");
	if (argc >= 2) {
		framesFolder = argv[1];
	}
	printf(framesFolder);
	printf("\"");
	const bool folderFound = (access(framesFolder, F_OK) == 0);
	if (!folderFound) {
		printf(" not found");
	}
	printf("\n\n");
	if (!folderFound) {
		#ifdef _WIN32
		printf("Press ESC to exit\n");

		while (1) {
			if (GetKeyState(VK_ESCAPE) & 0x8000) {
				break;
			}
			Sleep(10);
		}
		#else
		printf("Press any key to exit\n");
		wait_any_key();
		#endif

		return 0;
	}
	#ifdef _WIN32
	printf("Press ENTER to convert\n");

	while (1) {
		if (GetKeyState(VK_RETURN) & 0x8000) {
			break;
		}
		Sleep(10);
	}
	#else
	printf("Press any key to convert\n");
	wait_any_key();
	#endif
	//printf("E: Extract raw frames from source.rvid\n");

	char infoIniPath[256];
	sprintf(infoIniPath, "%s/info.ini", framesFolder);
	CIniFile info(infoIniPath);

	clear_screen();
	printf("Getting number of frames...\n");

	char framePath[256];
	// int foundFrames = info.GetInt("RVID", "FRAMES", -1);
	int foundFrames = -1;
	int foundBottomFrames = -1;

	while (1) {
		foundFrames++;
		sprintf(framePath, "%s/frame%i.png", framesFolder, foundFrames);
		if (access(framePath, F_OK) != 0) break;
	}
	foundFrames--;

	const int foundFramesDivided = foundFrames/8;

	if (foundFrames == -1) {
		clear_screen();
		printf("No frames have been found. Please extract the frames from a video file to the path here:\n\"");
		printf(framesFolder);
		printf("\"\n");
		printf("\n");
		#ifdef WIN32
		printf("Press ESC to exit\n");

		while (1) {
			if (GetKeyState(VK_ESCAPE) & 0x8000) {
				break;
			}
			Sleep(10);
		}
		#else
		printf("Press any key to exit\n");
		wait_any_key();
		#endif
		return 0;
	}

	gameConsole = info.GetInt("RVID", "GAME_CONSOLE", gameConsole);

	bool reviewInformation = false;
	bool gameConsoleEntered = false;

	if (gameConsole == 2) {
		clear_screen();
		printf("Which game console is the video for?\n");
		printf("1: GameBoy Advance\n");
		printf("2: Nintendo DS, DSi, and 3DS/2DS\n");

		selector = 0;

		while (selector < 1 || selector > 2) {
			scanf("%d", &selector);
		}

		gameConsole = selector-1;
		reviewInformation = true;
		gameConsoleEntered = true;
	}

	if (gameConsole == isNds) {
		while (1) {
			foundBottomFrames++;
			sprintf(framePath, "%s/bottom/frame%i.png", framesFolder, foundBottomFrames);
			if (access(framePath, F_OK) != 0) break;
		}
		foundBottomFrames--;

		if (foundBottomFrames != -1) {
			if (foundBottomFrames != foundFrames) {
				clear_screen();
				printf("The amount of top screen and bottom screen frames do not match.\n");
				printf("Make sure they're the same amount.\n");
				printf("\n");
				#ifdef WIN32
				printf("Press ESC to exit\n");

				while (1) {
					if (GetKeyState(VK_ESCAPE) & 0x8000) {
						break;
					}
					Sleep(10);
				}
				#else
				printf("Press any key to exit\n");
				wait_any_key();
				#endif
				return 0;
			}
			rvidHeader.dualScreen = 1;
		} else {
			rvidHeader.dualScreen = 0;
		}
	}

	rvidHeader.formatString = 0x44495652;	// "RVID"
	rvidHeader.ver = rvidVer;
	rvidHeader.frames = foundFrames+1;
	rvidHeader.bmpMode = info.GetInt("RVID", "BMP_MODE", 3);
	rvidHeader.fps = info.GetInt("RVID", "FPS", 0);
	bool fpsReduceBy01 = info.GetInt("RVID", "FPS_REDUCE_BY_0.1", 1);
	bool dsRefreshRate = info.GetInt("RVID", "FPS_DS_NATIVE", 0);
	bool widthDoubled = false;

	{
		sprintf(framePath, "%s/frame0.png", framesFolder);

		std::vector<unsigned char> image;
		unsigned width, height;
		lodepng::decode(image, width, height, framePath);
		if (gameConsole == isGba) {
			widthDoubled = (width == 480);
		} else {
			widthDoubled = (width == 512);
		}
		rvidHeader.vRes = (u8)height;
		if (widthDoubled) {
			rvidHeader.vRes /= 2;
		}
	}

	bool bmpModeEntered = false;

	if (rvidHeader.bmpMode == 3) {
		clear_screen();
		printf("Select the amount of colors to display on-screen.\n");
		printf("(Dithering will be applied to look like more is on-screen.)\n\n");
		printf("1: 256 (8 BPP, RGB565)\n");
		if (gameConsole == isGba || rvidHeader.dualScreen) {
			printf("- Frames will be interlaced if above 30 FPS\n");
		}
		printf("- Good quality\n");
		if (gameConsole == isNds) {
			printf("- Supports screen color filters\n");
		}
		printf("2: Unlimited (16 BPP, RGB555)\n");
		if (gameConsole == isGba || rvidHeader.dualScreen) {
			printf("- Frame Rate Limit: 30 FPS\n");
			printf("- Frames will be interlaced if above 15 FPS\n");
		} else {
			printf("- Frames will be interlaced if above 30 FPS\n");
		}
		printf("- High quality\n");
		printf("- Larger file size\n");
		if (gameConsole == isNds) {
			printf("- Does not support screen color filters\n");
		}
		printf("3: Unlimited (16 BPP, RGB565)\n");
		if (gameConsole == isGba || rvidHeader.dualScreen) {
			printf("- Frame Rate Limit: 30 FPS\n");
			printf("- Frames will be interlaced if above 15 FPS\n");
		} else {
			printf("- Frames will be interlaced if above 30 FPS\n");
		}
		printf("- Max quality\n");
		printf("- Larger file size\n");
		if (gameConsole == isNds) {
			printf("- Does not support screen color filters\n");
		}

		selector = 0;

		while (selector < 1 || selector > 3) {
			scanf("%d", &selector);
		}

		rvidHeader.bmpMode = selector - 1;
		selector = 0;
		reviewInformation = true;
		bmpModeEntered = true;
	}

	bool rvidFpsEntered = false;

	if (rvidHeader.fps == 0) {
		clear_screen();
		printf("What is the video's frame rate?\n");
		printf("1: 11.988 FPS\n");
		printf("2: 12     FPS\n");
		printf("3: 14.98  FPS\n");
		printf("4: 15     FPS\n");
		printf("5: 23.976 FPS\n");
		printf("6: 24     FPS\n");
		printf("7: 25     FPS\n");
		printf("8: 29.97  FPS\n");
		printf("9: 30     FPS\n");
		const bool hfrEnabled = ((gameConsole == isNds && !rvidHeader.dualScreen) || !rvidHeader.bmpMode);
		if (hfrEnabled) {
			printf("10: 47.952 FPS\n");
			printf("11: 48     FPS\n");
			printf("12: 50     FPS\n");
			printf("13: 59.826 FPS\n");
			printf("14: 59.94  FPS\n");
			printf("15: 60     FPS\n");
			if (gameConsole == isNds) {
				printf("16: 72     FPS\n");
			}
		}

		selector = 0;

		while (1) {
			scanf("%d", &selector);

			if (selector == 1) {
				rvidHeader.fps = 12;
				fpsReduceBy01 = true; // 11.988
				break;
			}
			if (selector == 2) {
				rvidHeader.fps = 12;
				fpsReduceBy01 = false;
				break;
			}
			if (selector == 3) {
				rvidHeader.fps = 15;
				fpsReduceBy01 = true; // 14.98
				break;
			}
			if (selector == 4) {
				rvidHeader.fps = 15;
				fpsReduceBy01 = false;
				break;
			}
			if (selector == 5) {
				rvidHeader.fps = 24;
				fpsReduceBy01 = true; // 23.976
				break;
			}
			if (selector == 6) {
				rvidHeader.fps = 24;
				fpsReduceBy01 = false;
				break;
			}
			if (selector == 7) {
				rvidHeader.fps = 25;
				fpsReduceBy01 = false;
				break;
			}
			if (selector == 8) {
				rvidHeader.fps = 30;
				fpsReduceBy01 = true; // 29.97
				break;
			}
			if (selector == 9) {
				rvidHeader.fps = 30;
				fpsReduceBy01 = false;
				break;
			}
			if (hfrEnabled) {
				if (selector == 10) {
					rvidHeader.fps = 48;
					fpsReduceBy01 = true; // 47.952
					break;
				}
				if (selector == 11) {
					rvidHeader.fps = 48;
					fpsReduceBy01 = false;
					break;
				}
				if (selector == 12) {
					rvidHeader.fps = 50;
					fpsReduceBy01 = false;
					break;
				}
				if (selector == 13) {
					rvidHeader.fps = 60;
					fpsReduceBy01 = false;
					dsRefreshRate = true;
					break;
				}
				if (selector == 14) {
					rvidHeader.fps = 60;
					fpsReduceBy01 = true; // 59.94
					break;
				}
				if (selector == 15) {
					rvidHeader.fps = 60;
					fpsReduceBy01 = false;
					break;
				}
				if (gameConsole == isNds && selector == 16) {
					rvidHeader.fps = 72;
					fpsReduceBy01 = false;
					break;
				}
			}
		}
		reviewInformation = true;
		rvidFpsEntered = true;
	}

	int fpsLimitForProgressiveScan = (gameConsole == isGba) ? 30 : 72;
	if (rvidHeader.dualScreen) {
		fpsLimitForProgressiveScan /= 2;
	}
	if (rvidHeader.bmpMode) {
		fpsLimitForProgressiveScan /= 2;
	}
	rvidHeader.interlaced = (rvidHeader.fps > fpsLimitForProgressiveScan) ? 1 : 0;
	int fpsLimitForCompressionSupport = (gameConsole == isGba) ? 15 : 50;
	int lowHeightForDoubleFps = (gameConsole == isGba) ? 100 : 108;
	if (rvidHeader.dualScreen) {
		fpsLimitForCompressionSupport /= 2;
	}
	if (rvidHeader.bmpMode) {
		fpsLimitForCompressionSupport /= 2;
	}
	if (rvidHeader.vRes <= lowHeightForDoubleFps) {
		fpsLimitForCompressionSupport *= 2;
	} else if (gameConsole == isNds && rvidHeader.bmpMode && rvidHeader.vRes > 144) {
		fpsLimitForCompressionSupport /= 2;
	}
	if (rvidHeader.interlaced) {
		fpsLimitForCompressionSupport *= 2;
	}
	if (rvidHeader.vRes > lowHeightForDoubleFps && fpsLimitForCompressionSupport > 48) {
		fpsLimitForCompressionSupport = 48;
	}
	/* framesCompressed = 0;
	if (rvidHeader.fps <= fpsLimitForCompressionSupport) {
		framesCompressed = info.GetInt("RVID", "COMPRESSED", 2);
	} */
	framesCompressed = (rvidHeader.fps <= fpsLimitForCompressionSupport);

	if (rvidHeader.interlaced) {
		rvidHeader.vRes /= 2;
	}

	/* if (rvidHeader.interlaced == 2) {
		clear_screen();
		printf("Interlace the video?\n");
		printf("This will reduce HFR video size in half.\n");
		printf("\n");
		printf("1: Yes\n");
		printf("0: No\n");

		selector = 0;

		scanf("%d", &selector);
		rvidHeader.interlaced = !(selector == 0);

	} */

	/* bool rvidCompressEntered = false;

	if (framesCompressed == 2) {
		clear_screen();
		printf("Compress the video frames to save some space?\n");
		printf("Video quality will not be affected.\n");
		printf("Depending on how may frames there are, this may take a while.\n");
		printf("\n");
		printf("1: Yes\n");
		printf("0: No\n");

		selector = 1;
		scanf("%d", &selector);
		framesCompressed = !(selector == 0);

		reviewInformation = true;
		rvidCompressEntered = true;
	} */

	bool rvidSoundEntered = false;
	bool rvidAudioBitModeEntered = false;

	char soundPath[256];
	char soundRightPath[256];
	sprintf(soundPath, "%s/sound.raw", framesFolder);
	if (access(soundPath, F_OK) != 0) {
		sprintf(soundPath, "%s/sound.raw.pcm", framesFolder);
	}
	sprintf(soundRightPath, "%s/soundRight.raw", framesFolder);
	if (access(soundRightPath, F_OK) != 0) {
		sprintf(soundRightPath, "%s/soundRight.raw.pcm", framesFolder);
	}
	bool soundFound = false;
	bool soundRightFound = false;
	u32 soundLeftSize = 0;
	u32 soundRightSize = 0;
	if (access(soundPath, F_OK) == 0) {
		soundLeftSize = getFileSize(soundPath);
		if (gameConsole != isGba && access(soundRightPath, F_OK) == 0) {
			soundRightSize = getFileSize(soundRightPath);
			soundRightFound = true;
		}
		rvidHeader.sampleRate = info.GetInt("RVID", "AUDIO_HZ", 0);
		rvidHeader.audioBitMode = info.GetInt("RVID", "AUDIO_BIT_MODE", 2);
		if (rvidHeader.sampleRate == 0) {
			clear_screen();
			printf("What is the audio sample rate?\n");
			// printf("0: Exclude sound\n");
			printf("1: 8000hz\n");
			printf("2: 11025hz\n");
			printf("3: 16000hz\n");
			printf("4: 22050hz\n");
			printf("5: 32000hz\n");

			selector = 5;

			while (1) {
				scanf("%d", &selector);

				/* if (selector == 0) {
					rvidHeader.hasSound = 0;
					break;
				} */
				if (selector == 1) {
					rvidHeader.sampleRate = 8000;
					break;
				}
				if (selector == 2) {
					rvidHeader.sampleRate = 11025;
					break;
				}
				if (selector == 3) {
					rvidHeader.sampleRate = 16000;
					break;
				}
				if (selector == 4) {
					rvidHeader.sampleRate = 22050;
					break;
				}
				if (selector == 5) {
					rvidHeader.sampleRate = 32000;
					break;
				}
			}
			reviewInformation = true;
			rvidSoundEntered = true;
		}
		if (rvidHeader.audioBitMode == 2) {
			clear_screen();
			printf("What is the encoding of the audio?\n");
			printf("1: 8-bit\n");
			printf("2: 16-bit");
			if (gameConsole == isGba) {
				printf(" (Will downconvert to 8-bit)");
			}
			printf("\n");

			while (1) {
				scanf("%d", &selector);

				if (selector == 1) {
					rvidHeader.audioBitMode = 0;
					break;
				}
				if (selector == 2) {
					rvidHeader.audioBitMode = 1;
					break;
				}
			}
			reviewInformation = true;
			rvidAudioBitModeEntered = true;
		}
		soundFound = true;
	} else {
		rvidHeader.sampleRate = 0;
		rvidHeader.audioBitMode = 0;
	}

	if (reviewInformation) {
		clear_screen();
		printf("Is the entered information correct?\n");
		if (gameConsoleEntered) {
			printf("- Game Console: ");
			switch (gameConsole) {
				case 0:
					printf("GameBoy Advance");
					break;
				case 1:
					printf("Nintendo DS, DSi, and 3DS/2DS");
					break;
			}
			printf("\n");
		}
		if (bmpModeEntered) {
			printf("- Color Amount: ");
			switch (rvidHeader.bmpMode) {
				case 0:
					printf("256 (8 BPP, RGB565)");
					break;
				case 1:
					printf("Unlimited (16 BPP, RGB555)");
					break;
				case 2:
					printf("Unlimited (16 BPP, RGB565)");
					break;
			}
			printf("\n");
		}
		if (rvidFpsEntered) {
			printf("- Frame Rate: ");
			switch (rvidHeader.fps) {
				case 12:
					printf(fpsReduceBy01 ? "11.988" : "12");
					break;
				case 15:
					printf(fpsReduceBy01 ? "14.98" : "15");
					break;
				case 24:
					printf(fpsReduceBy01 ? "23.976" : "24");
					break;
				case 25:
					printf("25");
					break;
				case 30:
					printf(fpsReduceBy01 ? "29.97" : "30");
					break;
				case 48:
					printf(fpsReduceBy01 ? "47.952" : "48");
					break;
				case 50:
					printf("50");
					break;
				case 60:
					printf(dsRefreshRate ? "59.8261" : fpsReduceBy01 ? "59.94" : "60");
					break;
				case 72:
					printf("72");
					break;
			}
			printf(" FPS\n");
		}
		/* if (rvidCompressEntered) {
			printf("- Compressed Frames: ");
			printf(framesCompressed ? "Yes" : "No");
			printf("\n");
		} */
		if (rvidSoundEntered || rvidAudioBitModeEntered) {
			printf("- Audio Quality: %ihz, %i-bit", rvidHeader.sampleRate, (rvidHeader.audioBitMode == 1) ? 16 : 8);
			if (gameConsole == isGba && rvidHeader.audioBitMode == 1) {
				printf(" (Will downconvert to 8-bit)");
			}
		}
		printf("\n");
		printf("1: Yes, save & proceed\n");
		printf("0: No, exit\n");


		selector = 1;
		scanf("%d", &selector);
		if (selector == 0) {
			return 0;
		}


		if (gameConsoleEntered) {
			info.SetInt("RVID", "GAME_CONSOLE", gameConsole);
		}
		if (bmpModeEntered) {
			info.SetInt("RVID", "BMP_MODE", rvidHeader.bmpMode);
		}
		if (rvidFpsEntered) {
			info.SetInt("RVID", "FPS", rvidHeader.fps);
			info.SetInt("RVID", "FPS_REDUCE_BY_0.1", fpsReduceBy01);
			info.SetInt("RVID", "FPS_DS_NATIVE", dsRefreshRate);
		}
		/* if (rvidCompressEntered) {
			info.SetInt("RVID", "COMPRESSED", framesCompressed);
		} */
		if (rvidSoundEntered) {
			info.SetInt("RVID", "AUDIO_HZ", rvidHeader.sampleRate);
		}
		if (rvidAudioBitModeEntered) {
			info.SetInt("RVID", "AUDIO_BIT_MODE", rvidHeader.audioBitMode);
		}
		info.SaveIniFileModified(infoIniPath);
	}

	char gbaPath[256];
	if (gameConsole == isGba) {
		char exePath[256] = {0};
		for (int i = strlen(argv[0]); i >= 0; i--) {
			if (argv[0][i] == '/' || argv[0][i] == '\\') {
				memcpy(exePath, argv[0], i);
				break;
			}
		}
		sprintf(gbaPath, "%s/rvid.gba", exePath);
		if (access(gbaPath, F_OK) != 0) {
			clear_screen();
			printf("\"rvid.gba\" not found in the same location as the .exe file.\n");
			printf("\n");
			#ifdef WIN32
			printf("Press ESC to exit\n");

			while (1) {
				if (GetKeyState(VK_ESCAPE) & 0x8000) {
					break;
				}
				Sleep(10);
			}
			#else
			printf("Press any key to exit\n");
			wait_any_key();
			#endif
			return 0;
		}
	}

	const bool downconvertAudio = (gameConsole == isGba && rvidHeader.audioBitMode == 1);
	if (downconvertAudio) rvidHeader.audioBitMode = 0;

	if (widthDoubled) {
		char flagPath[256];
		sprintf(flagPath, "%s/widthDoubled", framesFolder);
		#ifdef _WIN32
		if (access(flagPath, F_OK) != 0) {
			const u16 newLine = 0x0A0D;
			const char* line1 = "@echo Resizing frames, this may take a while...";
			const char* line2 = "@cd \"";
			const char* line2End = "\"";
			const char* line3 = "@magick mogrify -resize 256 *.png";
			if (gameConsole == isGba) {
				const char* line3 = "@magick mogrify -resize 240 *.png";
			}
			const char* line3_2 = "@cd bottom";
			const char* line3_3 = "@cd..";
			const char* line4 = "@mkdir widthDoubled";
			const char* line5 = "@echo Done!";
			const char* line6 = "@pause";

			FILE* batFile = fopen("Process Frames.bat", "wb");
			fwrite(line1, 1, strlen(line1), batFile);
			fwrite(&newLine, 2, 1, batFile);
			if (framesFolder[1] == ':') {
				char cdPathToDrive[7];
				sprintf(cdPathToDrive, "@cd C:");
				cdPathToDrive[4] = framesFolder[0];

				fwrite(cdPathToDrive, 1, 6, batFile);
				fwrite(&newLine, 2, 1, batFile);
			}
			fwrite(line2, 1, strlen(line2), batFile);
			fwrite(framesFolder, 1, strlen(framesFolder), batFile);
			fwrite(line2End, 1, strlen(line2End), batFile);
			fwrite(&newLine, 2, 1, batFile);
			fwrite(line3, 1, strlen(line3), batFile);
			fwrite(&newLine, 2, 1, batFile);
			if (rvidHeader.dualScreen) {
				fwrite(line3_2, 1, strlen(line3_2), batFile);
				fwrite(&newLine, 2, 1, batFile);
				fwrite(line3, 1, strlen(line3), batFile);
				fwrite(&newLine, 2, 1, batFile);
				fwrite(line3_3, 1, strlen(line3_3), batFile);
				fwrite(&newLine, 2, 1, batFile);
			}
			fwrite(line4, 1, strlen(line4), batFile);
			fwrite(&newLine, 2, 1, batFile);
			fwrite(line5, 1, strlen(line5), batFile);
			fwrite(&newLine, 2, 1, batFile);
			fwrite(line6, 1, strlen(line6), batFile);
			fclose(batFile);
		}

		while (access(flagPath, F_OK) != 0) {
			clear_screen();
			printf("Ensure ImageMagick is installed (with application directory added to system path),\n");
			printf("then open \"Process Frames.bat\".\n\n");
			printf("When the processing is done, press ENTER to continue...\n");

			while (1) {
				if (GetKeyState(VK_RETURN) & 0x8000) {
					break;
				}
				Sleep(10);
			}
		}

		remove(flagPath);

		if (access("Process Frames.bat", F_OK) == 0) {
			remove("Process Frames.bat");
		}
		#else
		if (access(flagPath, F_OK) != 0) {
			const char* newLine = "\n";
			const char* line1 = "echo Resizing frames, this may take a while...";
			const char* line2 = "cd \"";
			const char* line2End = "\"";
			const char* line3 = "magick mogrify -resize 256 *.png";
			if (gameConsole == isGba) {
				const char* line3 = "magick mogrify -resize 240 *.png";
			}
			const char* line3_2 = "cd bottom";
			const char* line3_3 = "cd..";
			const char* line4 = "mkdir widthDoubled";
			const char* line5 = "echo Done!";
			const char* line6 = "read -n1 -p \"Press enter to continue...\"";

			FILE* batFile = fopen("Process Frames.sh", "wb");
			fwrite(line1, 1, strlen(line1), batFile);
			fwrite(newLine, 1, 1, batFile);
			fwrite(line2, 1, strlen(line2), batFile);


			fwrite(framesFolder, 1, strlen(framesFolder), batFile);

			fwrite(line2End, 1, strlen(line2End), batFile);
			fwrite(newLine, 1, 1, batFile);
			fwrite(line3, 1, strlen(line3), batFile);
			fwrite(newLine, 1, 1, batFile);
			if (rvidHeader.dualScreen) {
				fwrite(line3_2, 1, strlen(line3_2), batFile);
				fwrite(newLine, 1, 1, batFile);
				fwrite(line3, 1, strlen(line3), batFile);
				fwrite(newLine, 1, 1, batFile);
				fwrite(line3_3, 1, strlen(line3_3), batFile);
				fwrite(newLine, 1, 1, batFile);
			}
			fwrite(line4, 1, strlen(line4), batFile);
			fwrite(newLine, 1, 1, batFile);
			fwrite(line5, 1, strlen(line5), batFile);
			fwrite(newLine, 1, 1, batFile);
			fwrite(line6, 1, strlen(line6), batFile);
			fclose(batFile);
		}

		while (access(flagPath, F_OK) != 0) {
			clear_screen();
			printf("Ensure ImageMagick is installed (and 'magick' command is working),\n");
			printf("then open \"Process Frames.sh\".\n\n");
			printf("When the processing is done, press any key to continue...\n");
			wait_any_key();
		}

		remove(flagPath);

		if (access("Process Frames.sh", F_OK) == 0) {
			remove("Process Frames.sh");
		}
		#endif
	}
	if (!rvidHeader.bmpMode) {
		char flagPath[256];
		sprintf(flagPath, "%s/dithered", framesFolder);
		if (access(flagPath, F_OK) != 0) {
			clear_screen();
			printf("Applying RGB565 dithering...\n");
			convertedFrames = -1;
			jobsDone = 0;
			if (foundFrames >= 128) {
				// Speed up process by running in 8 threads
				std::thread t1(applyRgb565Dither, 0, foundFramesDivided);
				std::thread t2(applyRgb565Dither, foundFramesDivided, foundFramesDivided*2);
				std::thread t3(applyRgb565Dither, foundFramesDivided*2, foundFramesDivided*3);
				std::thread t4(applyRgb565Dither, foundFramesDivided*3, foundFramesDivided*4);
				std::thread t5(applyRgb565Dither, foundFramesDivided*4, foundFramesDivided*5);
				std::thread t6(applyRgb565Dither, foundFramesDivided*5, foundFramesDivided*6);
				std::thread t7(applyRgb565Dither, foundFramesDivided*6, foundFramesDivided*7);
				std::thread t8(applyRgb565Dither, foundFramesDivided*7, foundFrames+1);

				while (jobsDone < 8) {
					if ((convertedFrames % 250) == 0) printf("\r%i/%i", convertedFrames, foundFrames);
					fflush(stdout);
				}
				// Ensure all threads are done running
				t1.join();
				t2.join();
				t3.join();
				t4.join();
				t5.join();
				t6.join();
				t7.join();
				t8.join();

				printf("\r%i/%i", convertedFrames, foundFrames);
				fflush(stdout);
			} else {
				applyRgb565Dither(0, foundFrames+1);
			}
			printf("\n");
			FILE* flagCreate = fopen(flagPath, "wb");
			fclose(flagCreate);
		}
		sprintf(flagPath, "%s/256colors", framesFolder);
		#ifdef _WIN32
		if (access(flagPath, F_OK) != 0) {
			if (access("Process Frames.bat", F_OK) != 0) {
				const u16 newLine = 0x0A0D;
				const char* line1 = "@echo Reducing color amount in each frame, this may take a while...";
				const char* line2 = "@cd \"";
				const char* line2End = "\"";
				const char* line3 = "@magick mogrify -colors 256 *.png";
				const char* line3_2 = "@cd bottom";
				const char* line3_3 = "@cd..";
				const char* line4 = "@mkdir 256colors";
				const char* line5 = "@echo Done!";
				const char* line6 = "@pause";

				FILE* batFile = fopen("Process Frames.bat", "wb");
				fwrite(line1, 1, strlen(line1), batFile);
				fwrite(&newLine, 2, 1, batFile);
				if (framesFolder[1] == ':') {
					char cdPathToDrive[7];
					sprintf(cdPathToDrive, "@cd C:");
					cdPathToDrive[4] = framesFolder[0];

					fwrite(cdPathToDrive, 1, 6, batFile);
					fwrite(&newLine, 2, 1, batFile);
				}
				fwrite(line2, 1, strlen(line2), batFile);
				fwrite(framesFolder, 1, strlen(framesFolder), batFile);
				fwrite(line2End, 1, strlen(line2End), batFile);
				fwrite(&newLine, 2, 1, batFile);
				fwrite(line3, 1, strlen(line3), batFile);
				fwrite(&newLine, 2, 1, batFile);
				if (rvidHeader.dualScreen) {
					fwrite(line3_2, 1, strlen(line3_2), batFile);
					fwrite(&newLine, 2, 1, batFile);
					fwrite(line3, 1, strlen(line3), batFile);
					fwrite(&newLine, 2, 1, batFile);
					fwrite(line3_3, 1, strlen(line3_3), batFile);
					fwrite(&newLine, 2, 1, batFile);
				}
				fwrite(line4, 1, strlen(line4), batFile);
				fwrite(&newLine, 2, 1, batFile);
				fwrite(line5, 1, strlen(line5), batFile);
				fwrite(&newLine, 2, 1, batFile);
				fwrite(line6, 1, strlen(line6), batFile);
				fclose(batFile);
			}

			while (access(flagPath, F_OK) != 0) {
				clear_screen();
				printf("Ensure ImageMagick is installed (with application directory added to system path),\n");
				printf("then open \"Process Frames.bat\".\n\n");
				printf("When the processing is done, press ENTER to continue...\n");

				while (1) {
					if (GetKeyState(VK_RETURN) & 0x8000) {
						break;
					}
					Sleep(10);
				}
			}
		}

		if (access("Process Frames.bat", F_OK) == 0) {
			remove("Process Frames.bat");
		}
		#else
		if (access(flagPath, F_OK) != 0) {
			if (access("Process Frames.sh", F_OK) != 0) {
				const char* newLine = "\n";
				const char* line1 = "echo Reducing color amount in each frame, this may take a while...";
				const char* line2 = "cd \"";
				const char* line2End = "\"";
				const char* line3 = "magick mogrify -colors 256 *.png";
				const char* line3_2 = "cd bottom";
				const char* line3_3 = "cd..";
				const char* line4 = "mkdir 256colors";
				const char* line5 = "echo Done!";
				const char* line6 = "read -n1 -p \"Press enter to continue...\"";

				FILE* batFile = fopen("Process Frames.sh", "wb");
				fwrite(line1, 1, strlen(line1), batFile);
				fwrite(newLine, 1, 1, batFile);
				fwrite(line2, 1, strlen(line2), batFile);

				fwrite(framesFolder, 1, strlen(framesFolder), batFile);

				fwrite(line2End, 1, strlen(line2End), batFile);
				fwrite(newLine, 1, 1, batFile);
				fwrite(line3, 1, strlen(line3), batFile);
				fwrite(newLine, 1, 1, batFile);
				if (rvidHeader.dualScreen) {
					fwrite(line3_2, 1, strlen(line3_2), batFile);
					fwrite(newLine, 1, 1, batFile);
					fwrite(line3, 1, strlen(line3), batFile);
					fwrite(newLine, 1, 1, batFile);
					fwrite(line3_3, 1, strlen(line3_3), batFile);
					fwrite(newLine, 1, 1, batFile);
				}
				fwrite(line4, 1, strlen(line4), batFile);
				fwrite(newLine, 1, 1, batFile);
				fwrite(line5, 1, strlen(line5), batFile);
				fwrite(newLine, 1, 1, batFile);
				fwrite(line6, 1, strlen(line6), batFile);
				fclose(batFile);
			}

			while (access(flagPath, F_OK) != 0) {
				clear_screen();
				printf("Ensure ImageMagick is installed (and 'magick' command is working),\n");
				printf("then open \"Process Frames.sh\".\n\n");
				printf("When the processing is done, press any key to continue...\n");
				wait_any_key();
			}
		}

		if (access("Process Frames.sh", F_OK) == 0) {
			remove("Process Frames.sh");
		}
		#endif
	}

	const int foundFramesTotal = foundFrames*(rvidHeader.dualScreen+1);
	if (gameConsole == isGba) {
		hRes = rvidHeader.bmpMode ? 240*2 : 240;
	} else {
		hRes = rvidHeader.bmpMode ? 256*2 : 256;
	}

	u32* frameOffset_lru = new u32[foundFramesTotal+1];
	memset(frameOffset_lru, 0xFF, (foundFramesTotal+1)*sizeof(u32));
	int* frameFileSize_lru = new int[foundFramesTotal+1];
	memset(frameFileSize_lru, 0xFF, (foundFramesTotal+1)*sizeof(int));
	char* convertedFramesSHA1[foundFramesTotal+1] = {NULL};
	// u16* palette_lru[rvidHeader.bmpMode ? 1 : foundFramesTotal+1] = {NULL};
	// u8* convertedFrame_lru[foundFramesTotal+1] = {NULL};

	for (int i = 0; i <= foundFrames; i++) {
		for (int b = 0; b < rvidHeader.dualScreen+1; b++) {
			frameOffsetTableSize += 4;

			const int num = rvidHeader.dualScreen ? (i*2)+b : i;
			/* if (!rvidHeader.bmpMode) {
				palette_lru[num] = new u16[256];
				memset(palette_lru[num], 0, 256*2);
			}
			const u32 len = rvidHeader.bmpMode ? (256*rvidHeader.vRes)*2 : 256*rvidHeader.vRes;
			convertedFrame_lru[num] = new u8[len];
			memset(convertedFrame_lru[num], 0, len); */
			convertedFramesSHA1[num] = new char[20];
		}
	}
	u32* frameOffsetTable = new u32[frameOffsetTableSize/4];
	u32* frameOffsetTableWithDupes = new u32[frameOffsetTableSize/4];
	memset(frameOffsetTable, 0, frameOffsetTableSize);
	memset(frameOffsetTableWithDupes, 0, frameOffsetTableSize);

	clear_screen();
	printf(framesCompressed ? "Converting and compressing...\n" : "Converting...\n");

	if (framesCompressed) {
		for (int i = 0; i <= foundFrames; i++) {
			for (int b = 0; b < rvidHeader.dualScreen+1; b++) {
				compressedFrameSizeTableSize += rvidHeader.bmpMode ? 4 : 2;
			}
		}
		if ((compressedFrameSizeTableSize % 4) != 0) {
			// Align table size to 4 bytes
			compressedFrameSizeTableSize += 2;
		}

		if (rvidHeader.bmpMode) {
			compressedFrameSizeTable32 = new u32[compressedFrameSizeTableSize/4];
			memset(compressedFrameSizeTable32, 0, compressedFrameSizeTableSize);
		} else {
			compressedFrameSizeTable16 = new u16[compressedFrameSizeTableSize/2];
			memset(compressedFrameSizeTable16, 0, compressedFrameSizeTableSize);
		}
	}

	FILE* tempFrames0 = fopen("tempFrames.0", "wb");
	FILE* tempFrames1 = fopen("tempFrames.1", "wb");
	FILE* tempFrames2 = fopen("tempFrames.2", "wb");
	FILE* tempFrames3 = fopen("tempFrames.3", "wb");
	FILE* tempFrames4 = fopen("tempFrames.4", "wb");
	FILE* tempFrames5 = fopen("tempFrames.5", "wb");
	FILE* tempFrames6 = fopen("tempFrames.6", "wb");
	FILE* tempFrames7 = fopen("tempFrames.7", "wb");

	convertedFrames = -1;
	jobsDone = 0;
	if (foundFrames >= 128) {
		// Speed up process by running in 8 threads
		std::thread t1(convertAndWriteFrames, tempFrames0, 0, 0, foundFramesDivided, frameOffsetTableWithDupes);
		std::thread t2(convertAndWriteFrames, tempFrames1, 1, foundFramesDivided, foundFramesDivided*2, frameOffsetTableWithDupes);
		std::thread t3(convertAndWriteFrames, tempFrames2, 2, foundFramesDivided*2, foundFramesDivided*3, frameOffsetTableWithDupes);
		std::thread t4(convertAndWriteFrames, tempFrames3, 3, foundFramesDivided*3, foundFramesDivided*4, frameOffsetTableWithDupes);
		std::thread t5(convertAndWriteFrames, tempFrames4, 4, foundFramesDivided*4, foundFramesDivided*5, frameOffsetTableWithDupes);
		std::thread t6(convertAndWriteFrames, tempFrames5, 5, foundFramesDivided*5, foundFramesDivided*6, frameOffsetTableWithDupes);
		std::thread t7(convertAndWriteFrames, tempFrames6, 6, foundFramesDivided*6, foundFramesDivided*7, frameOffsetTableWithDupes);
		std::thread t8(convertAndWriteFrames, tempFrames7, 7, foundFramesDivided*7, foundFrames+1, frameOffsetTableWithDupes);

		while (jobsDone < 8) {
			if ((convertedFrames % 250) == 0) printf("\r%i/%i", convertedFrames, foundFrames);
			fflush(stdout);
		}
		// Ensure all threads are done running
		t1.join();
		t2.join();
		t3.join();
		t4.join();
		t5.join();
		t6.join();
		t7.join();
		t8.join();

		printf("\r%i/%i", convertedFrames, foundFrames);
		fflush(stdout);
	} else {
		convertAndWriteFrames(tempFrames0, 0, 0, foundFrames+1, frameOffsetTableWithDupes);
	}

	fclose(tempFrames0);
	fclose(tempFrames1);
	fclose(tempFrames2);
	fclose(tempFrames3);
	fclose(tempFrames4);
	fclose(tempFrames5);
	fclose(tempFrames6);
	fclose(tempFrames7);
	printf("\n");

	printf("Setting up frame offset table and checking for duplicates...");

	FILE* tempFrames = fopen("tempFrames.bin", "wb");
	for (int i = 0; i <= foundFrames; i++) {
		for (int b = 0; b < rvidHeader.dualScreen+1; b++) {
			const int num = rvidHeader.dualScreen ? (i*2)+b : i;

			int frameFileSize = hRes*rvidHeader.vRes;
			if (framesCompressed) {
				if (rvidHeader.bmpMode) {
					frameFileSize = compressedFrameSizeTable32[num];
				} else {
					frameFileSize = compressedFrameSizeTable16[num];
				}
			}
			if (!rvidHeader.bmpMode) {
				frameFileSize += 0x200;
			}
			char writtenFramePath[16];
			int writtenFrameNum = 0;
			if (foundFrames >= 128) {
				if (num >= 0 && num < foundFramesDivided) {
					writtenFrameNum = 0;
				} else if (num >= foundFramesDivided && num < foundFramesDivided*2) {
					writtenFrameNum = 1;
				} else if (num >= foundFramesDivided*2 && num < foundFramesDivided*3) {
					writtenFrameNum = 2;
				} else if (num >= foundFramesDivided*3 && num < foundFramesDivided*4) {
					writtenFrameNum = 3;
				} else if (num >= foundFramesDivided*4 && num < foundFramesDivided*5) {
					writtenFrameNum = 4;
				} else if (num >= foundFramesDivided*5 && num < foundFramesDivided*6) {
					writtenFrameNum = 5;
				} else if (num >= foundFramesDivided*6 && num < foundFramesDivided*7) {
					writtenFrameNum = 6;
				} else if (num >= foundFramesDivided*7 && num < foundFrames+1) {
					writtenFrameNum = 7;
				}
			}
			sprintf(writtenFramePath, "tempFrames.%i", writtenFrameNum);
			FILE* writtenFrame = fopen(writtenFramePath, "rb");
			fseek(writtenFrame, frameOffsetTableWithDupes[num], SEEK_SET);
			fread(convertedFrame16[0], 1, frameFileSize, writtenFrame);
			fclose(writtenFrame);

			getFrameChecksum(frameFileSize);

			bool duplicateFrameFound = false;
			int duplicateFrame = 0;
			if (num > 0) {
				/* if (rvidHeader.bmpMode) {
					if (rvidHeader.interlaced) {
						for (int i2 = 0; i2 < lruCachePos; i2++) {
							duplicateFrameFound = memcmp(halvedFrame16, convertedFrame_lru[i2], 0x200*rvidHeader.vRes) == 0;
							if (duplicateFrameFound) {
								duplicateFrame = i2;
								break;
							}
						}
					} else {
						for (int i2 = 0; i2 < lruCachePos; i2++) {
							duplicateFrameFound = memcmp(convertedFrame16, convertedFrame_lru[i2], 0x200*rvidHeader.vRes) == 0;
							if (duplicateFrameFound) {
								duplicateFrame = i2;
								break;
							}
						}
					}
				} else {
					for (int i2 = 0; i2 < lruCachePos; i2++) {
						duplicateFrameFound = memcmp(palette, palette_lru[i2], 256*2) == 0;
						if (duplicateFrameFound) {
							if (rvidHeader.interlaced) {
								duplicateFrameFound = memcmp(halvedFrame, convertedFrame_lru[i2], 0x100*rvidHeader.vRes) == 0;
							} else {
								duplicateFrameFound = memcmp(convertedFrame, convertedFrame_lru[i2], 0x100*rvidHeader.vRes) == 0;
							}
							if (duplicateFrameFound) {
								duplicateFrame = i2;
								break;
							}
						}
					}
				} */
				for (int i2 = 0; i2 < lruCachePos; i2++) {
					if (memcmp(convertedFramesSHA1[i2], convertedFrameSHA1, 20) == 0) {
						duplicateFrameFound = true;
						duplicateFrame = i2;
						break;
					}
				}
			}

			if (!duplicateFrameFound) {
				/* if (rvidHeader.bmpMode) {
					if (rvidHeader.interlaced) {
						memcpy(convertedFrame_lru[lruCachePos], halvedFrame16, 0x200*rvidHeader.vRes);
					} else {
						memcpy(convertedFrame_lru[lruCachePos], convertedFrame16, 0x200*rvidHeader.vRes);
					}
				} else {
					memcpy(palette_lru[lruCachePos], palette, 256*2);
					if (rvidHeader.interlaced) {
						memcpy(convertedFrame_lru[lruCachePos], halvedFrame, 0x100*rvidHeader.vRes);
					} else {
						memcpy(convertedFrame_lru[lruCachePos], convertedFrame, 0x100*rvidHeader.vRes);
					}
				} */
				memcpy(convertedFramesSHA1[lruCachePos], convertedFrameSHA1, 20);

				fwrite(convertedFrame16[0], 1, frameFileSize, tempFrames);
			}

			if (num == 0) {
				frameOffsetTable[num] = 0x200+frameOffsetTableSize+compressedFrameSizeTableSize;
				sizeCheck = frameOffsetTable[num];
				sizeCheck += frameFileSize;
			} else {
				frameOffsetTable[num] = duplicateFrameFound ? frameOffset_lru[duplicateFrame] : frameOffset;

				if (!duplicateFrameFound) {
					const u32 sizeIncrease = frameFileSize_lru[previousFrame];

					const u32 sizeIncreaseForCheck = frameFileSize;
					sizeCheck += sizeIncreaseForCheck;
					if (gameConsole == isGba && sizeCheck >= (0x01FFE000-soundLeftSize)) {
						splitPointReached = 4;
					} else if (sizeCheck >= 0xFFFFFFFF) {
						splitPointReached++;
						frameOffsetTable[num] = splitPointReached;
						sizeCheck = sizeIncreaseForCheck;
					} else {
						frameOffsetTable[num] += sizeIncrease;
					}

					if (splitPointReached == 4) {
						fclose(tempFrames);
						printf(" Failed! Video is too big.\n");

						remove("tempFrames.0");
						remove("tempFrames.1");
						remove("tempFrames.2");
						remove("tempFrames.3");
						remove("tempFrames.4");
						remove("tempFrames.5");
						remove("tempFrames.6");
						remove("tempFrames.7");

						delete[] frameOffsetTable;
						delete[] frameOffsetTableWithDupes;

						remove("tempFrames.bin");
						delete[] frameOffset_lru;
						delete[] frameFileSize_lru;

						if (framesCompressed) {
							if (rvidHeader.bmpMode) {
								delete[] compressedFrameSizeTable32;
							} else {
								delete[] compressedFrameSizeTable16;
							}
						}

						#ifdef _WIN32
						printf("\nPress ESC to exit\n");

						while (1) {
							if (GetKeyState(VK_ESCAPE) & 0x8000) {
								break;
							}
							Sleep(10);
						}
						#else
						printf("\nPress any key to exit\n");
						wait_any_key();
						#endif

						return 0;
					}
				}
			}
			if (!duplicateFrameFound) {
				frameOffset = frameOffsetTable[num];
				frameOffset_lru[lruCachePos] = frameOffset;
				frameFileSize_lru[lruCachePos] = frameFileSize;
				previousFrame = lruCachePos;
				lruCachePos++;
				tempFramesSize += frameFileSize;
			}

			if ((b == 0) && rvidHeader.dualScreen) {
				sprintf(framePath, "%s/bottom/frame%i.png", framesFolder, i);
			}
		}
	}
	fclose(tempFrames);
	printf(" Done!\n");

	remove("tempFrames.0");
	remove("tempFrames.1");
	remove("tempFrames.2");
	remove("tempFrames.3");
	remove("tempFrames.4");
	remove("tempFrames.5");
	remove("tempFrames.6");
	remove("tempFrames.7");

	// delete[] convertedFramesSHA1;
	delete[] frameOffsetTableWithDupes;
	const u64 totalSizeNoFrames = 0x200+frameOffsetTableSize+compressedFrameSizeTableSize;
	const u64 totalSizeNoAudio = totalSizeNoFrames+tempFramesSize;
	const u64 totalSize = totalSizeNoAudio+soundLeftSize+soundRightSize;
	const bool splitRvid = (totalSize >= 0xFFFFFFFF);

	if (framesCompressed) {
		rvidHeader.compressedFrameSizeTableOffset = 0x200+frameOffsetTableSize;
	}
	if (splitRvid) {
		rvidHeader.soundLeftOffset = 0;
		rvidHeader.soundRightOffset = soundRightFound ? soundLeftSize : 0;
	} else {
		rvidHeader.soundLeftOffset = soundFound ? totalSizeNoAudio : 0;
		rvidHeader.soundRightOffset = soundRightFound ? totalSizeNoAudio+soundLeftSize : 0;
	}

	FILE* videoOutput[4] = {NULL};
	FILE* audioOutput = NULL;
	if (splitRvid) {
		if (totalSizeNoAudio < 0xFFFFFFFF) {
			videoOutput[0] = fopen("output.rvid", "wb");
		} else {
			char outputPath[24];
			for (int i = 0; i < splitPointReached+1; i++) {
				if (i == 0) {
					sprintf(outputPath, "output.rvid");
				} else {
					sprintf(outputPath, "output.rvid.%i", i);
				}
				videoOutput[i] = fopen(outputPath, "wb");
			}
		}
		audioOutput = fopen("output.rvidsnd", "wb");
	} else if (gameConsole == isGba) {
		videoOutput[0] = fopen("output.rvid.gba", "wb");
	} else {
		videoOutput[0] = fopen("output.rvid", "wb");
	}
	if (!videoOutput[0]) {
		printf("Failed to create rvid file\n");
		printf("\n");
		#ifdef _WIN32
		printf("Press ESC to exit\n");

		while (1) {
			if (GetKeyState(VK_ESCAPE) & 0x8000) {
				break;
			}
			Sleep(10);
		}
		#else
		printf("Press any key to exit\n");
		wait_any_key();
		#endif
		return 0;
	}

	if (dsRefreshRate) {
		rvidHeader.fps = 0;
	} else if (fpsReduceBy01) {
		rvidHeader.fps += 0x80;
	}

	if (gameConsole == isGba) {
		FILE* gba = fopen(gbaPath, "rb");
		memset(fileBuffer, 0, 0x2000);
		fread(fileBuffer, 1, 0x2000, gba);
		fwrite(fileBuffer, 1, 0x2000, videoOutput[0]);
		fclose(gba);

		rvidHeader.dualScreen = 2;
	}

	// Write header
	memcpy(headerToFile, &rvidHeader, sizeof(rvidHeaderInfo));
	fwrite(headerToFile, 1, 0x200, videoOutput[0]);

	fwrite(frameOffsetTable, 1, frameOffsetTableSize, videoOutput[0]);
	delete[] frameOffsetTable;

	printf("Adding frames...");

	if (framesCompressed) {
		if (rvidHeader.bmpMode) {
			fwrite(compressedFrameSizeTable32, 1, compressedFrameSizeTableSize, videoOutput[0]);
		} else {
			fwrite(compressedFrameSizeTable16, 1, compressedFrameSizeTableSize, videoOutput[0]);
		}
	}

	int numr = 0;

	tempFrames = fopen("tempFrames.bin", "rb");
	for (int i = 0; i < lruCachePos; i++) {
		// Add frames to .rvid file
		numr = fread(fileBuffer, 1, frameFileSize_lru[i], tempFrames);
		/* if (numr == 0) {
			printf("tempFrames.bin is not the expected size.\n");
			printf("\n");
			#ifdef _WIN32
			printf("Press ESC to exit\n");

			while (1) {
				if (GetKeyState(VK_ESCAPE) & 0x8000) {
					break;
				}
				Sleep(10);
			}
			#else
			printf("Press any key to exit\n");
			wait_any_key();
			#endif
			return 0;
		} */
		fwrite(fileBuffer, 1, numr, videoOutput[frameOffset_lru[i] % 4]);
	}
	fclose(tempFrames);
	printf(" Done!\n");

	remove("tempFrames.bin");
	delete[] frameOffset_lru;
	delete[] frameFileSize_lru;

	if (framesCompressed) {
		if (rvidHeader.bmpMode) {
			delete[] compressedFrameSizeTable32;
		} else {
			delete[] compressedFrameSizeTable16;
		}
	}

	if (soundFound) {
		printf("Adding sound...");

		u32 offset = 0;
		int numr;

		FILE* soundFile = fopen(soundPath, "rb");
		while (1)
		{
			// Add sound to .rvid file
			numr = fread(fileBuffer, 1, sizeof(fileBuffer), soundFile);
			if (downconvertAudio) {
				u16* fileBuffer16 = (u16*)fileBuffer;
				for (int i = 0; i < sizeof(fileBuffer)/2; i++) {
					fileBuffer[i] = fileBuffer16[i] / 0x100;
				}
				fwrite(fileBuffer, 1, numr/2, audioOutput ? audioOutput : videoOutput[0]);
			} else {
				fwrite(fileBuffer, 1, numr, audioOutput ? audioOutput : videoOutput[0]);
			}
			offset += sizeof(fileBuffer);

			if (offset > soundLeftSize) {
				break;
			}
		}
		fclose(soundFile);

		if (soundRightFound) {
			offset = 0;

			soundFile = fopen(soundRightPath, "rb");
			while (1)
			{
				// Add right-side sound to .rvid file
				numr = fread(fileBuffer, 1, sizeof(fileBuffer), soundFile);
				fwrite(fileBuffer, 1, numr, audioOutput ? audioOutput : videoOutput[0]);
				offset += sizeof(fileBuffer);

				if (offset > soundRightSize) {
					break;
				}
			}
			fclose(soundFile);
		}

		printf(" Done!\n");
	}

	for (int i = 0; i < splitPointReached+1; i++) {
		fclose(videoOutput[i]);
	}
	if (audioOutput) {
		fclose(audioOutput);
	}

	Sleep(1000);

	return 0;
}