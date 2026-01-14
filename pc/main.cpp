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
#include <Windows.h>

#include "graphics/lodepng.h"
#include "lz77.h"
#include "sha1.h"
#include "inifile.h"

#define lowHeightForDoubleFps 108

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;

template<class TYPE> inline TYPE BIT(const TYPE & x)
{ return TYPE(1) << x; }

void clear_screen(char fill = ' ') {
	COORD tl = {0,0};
	CONSOLE_SCREEN_BUFFER_INFO s;
	HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
	GetConsoleScreenBufferInfo(console, &s);
	DWORD written, cells = s.dwSize.X * s.dwSize.Y;
	FillConsoleOutputCharacter(console, fill, cells, tl, &written);
	FillConsoleOutputAttribute(console, s.wAttributes, cells, tl, &written);
	SetConsoleCursorPosition(console, tl);
}

static bool bottomField[2] = {false};
static int splitPointReached = 0;
static int previousFrame = 0;
static int lruCachePos = 0;

bool paletteSet[256] = {false};
u16 palette[256] = {0};

u8 convertedFrame[256*192];
u16 convertedFrame16[256*192];
u8 halvedFrame[256*96];
u16 halvedFrame16[256*96];
char convertedFrameSHA1[20];
unsigned char* compressedFrame;

char fileBuffer[0x100000] = {0};
u32 frameOffsetTableSize = 0;
u32 frameOffset = 0;
u32 compressedFrameSizeTableSize = 0;
u64 tempFramesSize = 0;
u32 soundSize = 0;
u64 sizeCheck = 0;

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

#define rvidVer 4

typedef struct rvidHeaderInfo {
	u32 formatString;  	    // "RVID" string
	u32 ver;			    // File format version
	u32 frames;			    // Number of frames
	u8 fps;				    // Frames per second
	u8 vRes;			    // Vertical resolution
	u8 interlaced;		    // Is interlaced
	u8 dualScreen;		    // Is dual screen video
	u16 sampleRate;			// Audio sample rate
	u8 audioBitMode;		// 0 = 8-bit, 1 = 16-bit
	u8 bmpMode;        		// 0 = 8 BPP (RGB565), 1 = 16 BPP (RGB555), 2 = 16 BPP (RGB565)
	u32 compressedFrameSizeTableOffset;		// Offset of compressed frame size table
	u32 soundLeftOffset;		// Offset of left-side sound stream
	u32 soundRightOffset;		// Offset of right-side sound stream
} rvidHeaderInfo;

rvidHeaderInfo rvidHeader;
const char* framesFolder = "rvidFrames";

#define titleText "Vid2RVID v1.6\nby Rocket Robz\n"

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

void convertFrame(int b, unsigned width, std::vector<unsigned char> image, bool alternatePixel) {
	if (!rvidHeader.bmpMode) {
		for (int i = 0; i < 256; i++) {
			paletteSet[i] = false;
			palette[i] = 0;
		}
	}

	int x = 0;
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
			convertedFrame16[i] = color;

			x++;
			if ((unsigned)x == width) {
				alternatePixel = !alternatePixel;
				x=0;
			}
			alternatePixel = !alternatePixel;
		} else {
			int p = 0;
			for (p = 0; p < 256; p++) {
				if (!paletteSet[p]) {
					palette[p] = color;
					paletteSet[p] = true;
					break;
				} else if (palette[p] == color) {
					break;
				}
			}
			convertedFrame[i] = p;
		}
	}

	if (rvidHeader.bmpMode) {
		if (rvidHeader.interlaced) {
			int f = bottomField[b] ? 1 : 0;
			int x = 0;
			for(int i = 0; i < 256*rvidHeader.vRes; i++) {
				halvedFrame16[i] = convertedFrame16[(256*f)+x];
				x++;
				if (x == 256) {
					f += 2;
					x = 0;
				}
			}
			SHA1(convertedFrameSHA1, (char*)halvedFrame16, (256*rvidHeader.vRes)*2);
			bottomField[b] = !bottomField[b];
		} else {
			SHA1(convertedFrameSHA1, (char*)convertedFrame16, (256*rvidHeader.vRes)*2);
		}
	} else {
		SHA1_CTX ctx;
		unsigned int ii;
		char* convertedFrameChar = (char*)palette;

		SHA1Init(&ctx);
		for (ii=0; ii<256*2; ii+=1)
			SHA1Update(&ctx, (const unsigned char*)convertedFrameChar + ii, 1);

		if (rvidHeader.interlaced) {
			char* convertedFrameChar = (char*)halvedFrame;

			int f = bottomField[b] ? 1 : 0;
			int x = 0;
			for(int i = 0; i < 256*rvidHeader.vRes; i++) {
				halvedFrame[i] = convertedFrame[(256*f)+x];
				x++;
				if (x == 256) {
					f += 2;
					x = 0;
				}
			}
			for (ii=0; ii<256*rvidHeader.vRes; ii+=1)
				SHA1Update(&ctx, (const unsigned char*)convertedFrameChar + ii, 1);

			bottomField[b] = !bottomField[b];
		} else {
			char* convertedFrameChar = (char*)convertedFrame;

			for (ii=0; ii<256*rvidHeader.vRes; ii+=1)
				SHA1Update(&ctx, (const unsigned char*)convertedFrameChar + ii, 1);
		}
		SHA1Final((unsigned char *)convertedFrameSHA1, &ctx);
	}
}

int main(int argc, char **argv) {

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
		printf("Press ESC to exit\n");

		while (1) {
			if (GetKeyState(VK_ESCAPE) & 0x8000) {
				break;
			}
			Sleep(10);
		}

		return 0;
	}
	printf("Press ENTER to convert\n");
	//printf("E: Extract raw frames from source.rvid\n");

	while (1) {
		if (GetKeyState(VK_RETURN) & 0x8000) {
			break;
		}
		/*if (GetKeyState('E') & 0x8000) {
			extractFrames();
			return 0;
			break;
		}*/
		Sleep(10);
	}

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

	if (foundFrames == -1) {
		clear_screen();
		printf("No frames have been found. Please extract the frames from a video file to the path here:\n\"");
		printf(framesFolder);
		printf("\"\n");
		printf("\n");
		printf("Press ESC to exit\n");

		while (1) {
			if (GetKeyState(VK_ESCAPE) & 0x8000) {
				break;
			}
			Sleep(10);
		}

		return 0;
	}

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
			printf("Press ESC to exit\n");

			while (1) {
				if (GetKeyState(VK_ESCAPE) & 0x8000) {
					break;
				}
				Sleep(10);
			}

			return 0;
		}
		rvidHeader.dualScreen = 1;
	} else {
		rvidHeader.dualScreen = 0;
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
		widthDoubled = (width == 512);
		rvidHeader.vRes = (u8)height;
		if (widthDoubled) {
			rvidHeader.vRes /= 2;
		}
	}

	bool reviewInformation = false;
	bool bmpModeEntered = false;

	if (rvidHeader.bmpMode == 3) {
		clear_screen();
		printf("Select the amount of colors to display on-screen.\n");
		printf("(Dithering will be applied to look like more is on-screen.)\n\n");
		printf("1: 256 (8 BPP, RGB565)\n");
		printf("- Good quality\n");
		printf("- Recommended due to low file size and high frame rate support\n");
		printf("- Supports screen color filters\n");
		printf("2: Unlimited (16 BPP, RGB555)\n");
		if (rvidHeader.dualScreen) {
			printf("- Frame Rate Limit: 30 FPS\n");
		}
		printf("- High quality\n");
		printf("- Large file size\n");
		printf("- Does not support screen color filters\n");
		printf("3: Unlimited (16 BPP, RGB565)\n");
		if (rvidHeader.dualScreen) {
			printf("- Frame Rate Limit: 30 FPS\n");
		}
		printf("- High quality\n");
		printf("- Increased green color range\n");
		printf("- Large file size\n");
		printf("- Does not support screen color filters\n");
		Sleep(100);

		while (1) {
			if (GetKeyState('1') & 0x8000) {
				rvidHeader.bmpMode = 0;
				break;
			}
			if (GetKeyState('2') & 0x8000) {
				rvidHeader.bmpMode = 1;
				break;
			}
			if (GetKeyState('3') & 0x8000) {
				rvidHeader.bmpMode = 2;
				break;
			}
			Sleep(10);
		}
		reviewInformation = true;
		bmpModeEntered = true;
		Sleep(10);
	}

	bool rvidFpsEntered = false;

	if (rvidHeader.fps == 0) {
		clear_screen();
		printf("What is the video's frame rate?\n");
		printf("1: 11.988 FPS (Right -> key held: 12 FPS)\n");
		printf("2: 14.98 FPS (Right -> key held: 15 FPS)\n");
		printf("3: 23.976 FPS (Right -> key held: 24 FPS)\n");
		printf("4: 25 FPS\n");
		printf("5: 29.97 FPS (Right -> key held: 30 FPS)\n");
		if (!rvidHeader.dualScreen || !rvidHeader.bmpMode) {
			printf("6: 47.952 FPS (Right -> key held: 48 FPS)\n");
			printf("7: 50 FPS\n");
			printf("8: 59.94 FPS (Down \\|/ key held: 59.8261 FPS, Right -> key held: 60 FPS)\n");
			printf("9: 72 FPS\n");
		}
		Sleep(100);

		while (1) {
			fpsReduceBy01 = !(GetKeyState(VK_RIGHT) & 0x8000);
			if (GetKeyState('1') & 0x8000) {
				rvidHeader.fps = 12;
				break;
			}
			if (GetKeyState('2') & 0x8000) {
				rvidHeader.fps = 15;
				break;
			}
			if (GetKeyState('3') & 0x8000) {
				rvidHeader.fps = 24;
				break;
			}
			if (GetKeyState('4') & 0x8000) {
				rvidHeader.fps = 25;
				fpsReduceBy01 = false;
				break;
			}
			if (GetKeyState('5') & 0x8000) {
				rvidHeader.fps = 30;
				break;
			}
			if (!rvidHeader.dualScreen || !rvidHeader.bmpMode) {
				if (GetKeyState('6') & 0x8000) {
					rvidHeader.fps = 48;
					break;
				}
				if (GetKeyState('7') & 0x8000) {
					rvidHeader.fps = 50;
					fpsReduceBy01 = false;
					break;
				}
				if (GetKeyState('8') & 0x8000) {
					rvidHeader.fps = 60;
					if (fpsReduceBy01 && (GetKeyState(VK_DOWN) & 0x8000)) {
						fpsReduceBy01 = false;
						dsRefreshRate = true;
					}
					break;
				}
				if (GetKeyState('9') & 0x8000) {
					rvidHeader.fps = 72;
					fpsReduceBy01 = false;
					break;
				}
			}
			Sleep(10);
		}
		reviewInformation = true;
		rvidFpsEntered = true;
		Sleep(10);
	}

	int fpsLimitForProgressiveScan = (rvidHeader.dualScreen ? 30 : 72);
	if (rvidHeader.bmpMode) {
		fpsLimitForProgressiveScan /= 2;
	}
	rvidHeader.interlaced = (rvidHeader.fps > fpsLimitForProgressiveScan) ? 1 : 0;
	int fpsLimitForCompressionSupport = (rvidHeader.dualScreen ? 25 : 50);
	if (rvidHeader.bmpMode) {
		fpsLimitForCompressionSupport /= 2;
	}
	if (rvidHeader.vRes <= lowHeightForDoubleFps) {
		fpsLimitForCompressionSupport *= 2;
	} else if (rvidHeader.bmpMode && rvidHeader.vRes > 144) {
		fpsLimitForCompressionSupport /= 2;
	}
	if (rvidHeader.interlaced) {
		fpsLimitForCompressionSupport *= 2;
	}
	int framesCompressed = 0;
	if (rvidHeader.fps <= fpsLimitForCompressionSupport) {
		framesCompressed = info.GetInt("RVID", "COMPRESSED", 2);
	}

	if (rvidHeader.interlaced) {
		rvidHeader.vRes /= 2;
	}

	/* if (rvidHeader.interlaced == 2) {
		clear_screen();
		printf("Interlace the video?\n");
		printf("This will reduce HFR video size in half.\n");
		printf("\n");
		printf("Y: Yes\n");
		printf("N: No\n");

		while (1) {
			if (GetKeyState('Y') & 0x8000) {
				rvidHeader.interlaced = 1;
				break;
			}
			if (GetKeyState('N') & 0x8000) {
				rvidHeader.interlaced = 0;
				break;
			}
		}
	} */

	bool rvidCompressEntered = false;

	if (framesCompressed == 2) {
		clear_screen();
		printf("Compress the video frames to save some space?\n");
		printf("Video quality will not be affected.\n");
		printf("Depending on how may frames you have, this may take a while.\n");
		printf("\n");
		printf("Y: Yes\n");
		printf("N: No\n");
		Sleep(100);

		while (1) {
			if (GetKeyState('Y') & 0x8000) {
				framesCompressed = 1;
				break;
			}
			if (GetKeyState('N') & 0x8000) {
				framesCompressed = 0;
				break;
			}
			Sleep(10);
		}
		reviewInformation = true;
		rvidCompressEntered = true;
		Sleep(10);
	}

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
		if (access(soundRightPath, F_OK) == 0) {
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
			Sleep(100);

			while (1) {
				/* if (GetKeyState('0') & 0x8000) {
					rvidHeader.hasSound = 0;
					break;
				} */
				if (GetKeyState('1') & 0x8000) {
					rvidHeader.sampleRate = 8000;
					break;
				}
				if (GetKeyState('2') & 0x8000) {
					rvidHeader.sampleRate = 11025;
					break;
				}
				if (GetKeyState('3') & 0x8000) {
					rvidHeader.sampleRate = 16000;
					break;
				}
				if (GetKeyState('4') & 0x8000) {
					rvidHeader.sampleRate = 22050;
					break;
				}
				if (GetKeyState('5') & 0x8000) {
					rvidHeader.sampleRate = 32000;
					break;
				}
				Sleep(10);
			}
			reviewInformation = true;
			rvidSoundEntered = true;
			Sleep(10);
		}
		if (rvidHeader.audioBitMode == 2) {
			clear_screen();
			printf("What is the encoding of the audio?\n");
			printf("1: 8-bit\n");
			printf("2: 16-bit\n");
			Sleep(100);

			while (1) {
				if (GetKeyState('1') & 0x8000) {
					rvidHeader.audioBitMode = 0;
					break;
				}
				if (GetKeyState('2') & 0x8000) {
					rvidHeader.audioBitMode = 1;
					break;
				}
				Sleep(10);
			}
			reviewInformation = true;
			rvidAudioBitModeEntered = true;
			Sleep(10);
		}
		soundFound = true;
	} else {
		rvidHeader.sampleRate = 0;
		rvidHeader.audioBitMode = 0;
	}

	if (reviewInformation) {
		clear_screen();
		printf("Is the entered information correct?\n");
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
		if (rvidCompressEntered) {
			printf("- Compressed Frames: ");
			printf(framesCompressed ? "Yes" : "No");
			printf("\n");
		}
		if (rvidSoundEntered || rvidAudioBitModeEntered) {
			printf("- Audio Quality: %ihz, %i-bit", rvidHeader.sampleRate, (rvidHeader.audioBitMode == 1) ? 16 : 8);
		}
		printf("\n");
		printf("Y: Yes, save & proceed\n");
		printf("N: No, exit\n");
		Sleep(100);

		while (1) {
			if (GetKeyState('Y') & 0x8000) {
				break;
			}
			if (GetKeyState('N') & 0x8000) {
				return 0;
			}
			Sleep(10);
		}
		Sleep(10);

		if (bmpModeEntered) {
			info.SetInt("RVID", "BMP_MODE", rvidHeader.bmpMode);
		}
		if (rvidFpsEntered) {
			info.SetInt("RVID", "FPS", rvidHeader.fps);
			info.SetInt("RVID", "FPS_REDUCE_BY_0.1", fpsReduceBy01);
			info.SetInt("RVID", "FPS_DS_NATIVE", dsRefreshRate);
		}
		if (rvidCompressEntered) {
			info.SetInt("RVID", "COMPRESSED", framesCompressed);
		}
		if (rvidSoundEntered) {
			info.SetInt("RVID", "AUDIO_HZ", rvidHeader.sampleRate);
		}
		if (rvidAudioBitModeEntered) {
			info.SetInt("RVID", "AUDIO_BIT_MODE", rvidHeader.audioBitMode);
		}
		info.SaveIniFileModified(infoIniPath);
	}

	// if (!rvidHeader.bmpMode)
	{
		char flagPath[256];
		sprintf(flagPath, "%s/256colors", framesFolder);
		const bool flagFound = (access(flagPath, F_OK) == 0);
		if ((!rvidHeader.bmpMode && !flagFound) || widthDoubled) {
			if (access("Process Frames.bat", F_OK) != 0) {
				const u16 newLine = 0x0A0D;
				const char* line1 = "@echo Processing frames, this may take a while...";
				const char* line2 = "@cd \"";
				const char* line2End = "\"";
				const char* line3 = "";
				if (!rvidHeader.bmpMode && !flagFound) {
					if (widthDoubled) {
						line3 = "@magick mogrify -resize 256 -ordered-dither checks,32,64,32 -colors 256 *.png";
					} else {
						line3 = "@magick mogrify -ordered-dither checks,32,64,32 -colors 256 *.png";
					}
				} else if (widthDoubled) {
					line3 = "@magick mogrify -resize 256 *.png";
				}
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
				if (!rvidHeader.bmpMode && !flagFound) {
					fwrite(line4, 1, strlen(line4), batFile);
					fwrite(&newLine, 2, 1, batFile);
				}
				fwrite(line5, 1, strlen(line5), batFile);
				fwrite(&newLine, 2, 1, batFile);
				fwrite(line6, 1, strlen(line6), batFile);
				fclose(batFile);
			}

			while (access(flagPath, F_OK) != 0) {
				clear_screen();
				printf("Ensure ImageMagick is installed (with application directory added to system path),\n");
				printf("then open \"Process Frames.bat\".\n\n");
				printf("When the processing is done, press ENTER.\n");
				Sleep(100);

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
	}

	const int foundFramesTotal = foundFrames*(rvidHeader.dualScreen+1);
	const int hRes = rvidHeader.bmpMode ? 0x200 : 0x100;

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
	memset(frameOffsetTable, 0, frameOffsetTableSize);

	u32* compressedFrameSizeTable32 = NULL;
	u16* compressedFrameSizeTable16 = NULL;

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

	FILE* tempFrames = fopen("tempFrames.bin", "wb");
	for (int i = 0; i <= foundFrames; i++) {
		sprintf(framePath, "%s/frame%i.png", framesFolder, i);
		if (access(framePath, F_OK) == 0) {
			for (int b = 0; b < rvidHeader.dualScreen+1; b++) {
				const int num = rvidHeader.dualScreen ? (i*2)+b : i;

				std::vector<unsigned char> image;
				unsigned width, height;
				lodepng::decode(image, width, height, framePath);

				if ((b == 0) && ((i % 500) == 0)) printf("%i/%i\n", i, foundFrames);

				convertFrame(b, width, image, !rvidHeader.interlaced && (i % 2));

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

					// Save current frame to temp file
					if (!rvidHeader.bmpMode) {
						fwrite(palette, 2, 256, tempFrames);
					}

					if (framesCompressed) {
						if (rvidHeader.bmpMode) {
							if (rvidHeader.interlaced) {
								compressedFrame = lzssCompress((unsigned char*)halvedFrame16, 0x200*rvidHeader.vRes);
							} else {
								compressedFrame = lzssCompress((unsigned char*)convertedFrame16, 0x200*rvidHeader.vRes);
							}
						} else {
							if (rvidHeader.interlaced) {
								compressedFrame = lzssCompress((unsigned char*)halvedFrame, 0x100*rvidHeader.vRes);
							} else {
								compressedFrame = lzssCompress((unsigned char*)convertedFrame, 0x100*rvidHeader.vRes);
							}
						}
					}

					if (!framesCompressed || (frameFileSize >= hRes*rvidHeader.vRes)) {
						// Store uncompressed frame if compressed frame is exactly the same size or larger, or if compression is disabled
						frameFileSize = hRes*rvidHeader.vRes;
						if (rvidHeader.bmpMode) {
							fwrite(rvidHeader.interlaced ? halvedFrame16 : convertedFrame16, 1, frameFileSize, tempFrames);
						} else {
							fwrite(rvidHeader.interlaced ? halvedFrame : convertedFrame, 1, frameFileSize, tempFrames);
						}
					} else {
						fwrite(compressedFrame, 1, frameFileSize, tempFrames);
					}
				}

				if (num == 0) {
					frameOffsetTable[num] = 0x200+frameOffsetTableSize+compressedFrameSizeTableSize;
					sizeCheck = frameOffsetTable[num];
					if (!rvidHeader.bmpMode) {
						sizeCheck += 0x200;
					}
					sizeCheck += frameFileSize;
				} else {
					frameOffsetTable[num] = duplicateFrameFound ? frameOffset_lru[duplicateFrame] : frameOffset;

					if (!duplicateFrameFound) {
						const u32 sizeIncrease = (rvidHeader.bmpMode ? 0 : 0x200) + frameFileSize_lru[previousFrame];

						if (splitPointReached < 3) {
							const u32 sizeIncreaseForCheck = (rvidHeader.bmpMode ? 0 : 0x200) + frameFileSize;
							sizeCheck += sizeIncreaseForCheck;
							if (sizeCheck >= 0xFFFFFFFF) {
								splitPointReached++;
								frameOffsetTable[num] = splitPointReached;
								sizeCheck = sizeIncreaseForCheck;
							} else {
								frameOffsetTable[num] += sizeIncrease;
							}
						} else {
							frameOffsetTable[num] += sizeIncrease;
						}
					}
				}
				if (framesCompressed) {
					if (rvidHeader.bmpMode) {
						compressedFrameSizeTable32[num] = duplicateFrameFound ? frameFileSize_lru[duplicateFrame] : frameFileSize;
					} else {
						compressedFrameSizeTable16[num] = duplicateFrameFound ? frameFileSize_lru[duplicateFrame] : frameFileSize;
					}
				}
				if (!duplicateFrameFound) {
					frameOffset = frameOffsetTable[num];
					frameOffset_lru[lruCachePos] = frameOffset;
					frameFileSize_lru[lruCachePos] = frameFileSize;
					previousFrame = lruCachePos;
					lruCachePos++;
					if (!rvidHeader.bmpMode) {
						tempFramesSize += 0x200;
					}
					tempFramesSize += frameFileSize;
					if (framesCompressed) {
						delete[] compressedFrame;
					}
				}

				if ((b == 0) && rvidHeader.dualScreen) {
					sprintf(framePath, "%s/bottom/frame%i.png", framesFolder, i);
				}
			}
		} else {
			break;
		}
	}
	fclose(tempFrames);

	// delete[] convertedFramesSHA1;
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
				sprintf(outputPath, "output.part%i.rvid", i+1);
				videoOutput[i] = fopen(outputPath, "wb");
			}
		}
		audioOutput = fopen("output.audio.rvid", "wb");
	} else {
		videoOutput[0] = fopen("output.rvid", "wb");
	}
	if (!videoOutput[0]) {
		clear_screen();
		printf("Failed to create rvid file\n");
		printf("\n");
		printf("Press ESC to exit\n");

		while (1) {
			if (GetKeyState(VK_ESCAPE) & 0x8000) {
				break;
			}
			Sleep(10);
		}

		return 0;
	}

	if (dsRefreshRate) {
		rvidHeader.fps = 0;
	} else if (fpsReduceBy01) {
		rvidHeader.fps += 0x80;
	}

	// Write header
	memcpy(headerToFile, &rvidHeader, sizeof(rvidHeaderInfo));
	fwrite(headerToFile, 1, 0x200, videoOutput[0]);

	fwrite(frameOffsetTable, 1, frameOffsetTableSize, videoOutput[0]);
	delete[] frameOffsetTable;

	clear_screen();
	printf("Adding frames...\n");

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
		numr = fread(fileBuffer, 1, (rvidHeader.bmpMode ? 0 : 0x200) + frameFileSize_lru[i], tempFrames);
		/* if (numr == 0) {
			clear_screen();
			printf("tempFrames.bin is not the expected size.\n");
			printf("\n");
			printf("Press ESC to exit\n");

			while (1) {
				if (GetKeyState(VK_ESCAPE) & 0x8000) {
					break;
				}
				Sleep(10);
			}

			return 0;
		} */
		fwrite(fileBuffer, 1, numr, videoOutput[frameOffset_lru[i] % 4]);
	}
	fclose(tempFrames);

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
		clear_screen();
		printf("Adding sound...\n");

		u32 offset = 0;
		int numr;

		FILE* soundFile = fopen(soundPath, "rb");
		while (1)
		{
			// Add sound to .rvid file
			numr = fread(fileBuffer, 1, sizeof(fileBuffer), soundFile);
			fwrite(fileBuffer, 1, numr, audioOutput ? audioOutput : videoOutput[0]);
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
	}

	for (int i = 0; i < splitPointReached+1; i++) {
		fclose(videoOutput[i]);
	}
	if (audioOutput) {
		fclose(audioOutput);
	}

	remove("tempFrames.bin");

	clear_screen();
	printf("Done!\n");

	return 0;
}
