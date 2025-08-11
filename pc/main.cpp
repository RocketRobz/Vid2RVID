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
#include "inifile.h"

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
static bool currentFrame = false;

bool paletteSet[256] = {false};
u16 palette[2][256] = {0};

u8 convertedFrame[2][256*192];
u16 convertedFrame16[2][256*192];
u8 halvedFrame[2][256*96];
u16 halvedFrame16[2][256*96];
unsigned char* compressedFrame;

char fileBuffer[0x100000] = {0};
u32 frameOffsetTableSize = 0;
int previousFrameFileSize = 0;
u32 compressedFrameSizeTableSize = 0;
u32 tempFramesSize = 0;
u32 soundSize = 0;

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

#define rvidVer 3

typedef struct rvidHeaderInfo {
	u32 formatString;  	    // "RVID" string
	u32 ver;			    // File format version
	u32 frames;			    // Number of frames
	u8 fps;				    // Frames per second
	u8 vRes;			    // Vertical resolution
	u8 interlaced;		    // Is interlaced
	u8 dualScreen;		    // Is dual screen video
	u16 sampleRate;		    // Audio sample rate
	u8 framesCompressed;	// Frames are compressed (unused)
	u8 bmpMode;        		// 0 = 256 RGB565 colors, 1 = Unlimited RGB555 colors, 2 = Unlimited RGB565 colors
	u32 compressedFrameSizeTableOffset;		// Offset of compressed frame size table
	u32 soundOffset;		// Offset of sound stream
} rvidHeaderInfo;

rvidHeaderInfo rvidHeader;
const char* framesFolder = "rvidFrames";

#define titleText "Vid2RVID v1.4\nby Rocket Robz\n"

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

void convertFrame(int b, unsigned width, std::vector<unsigned char> image) {
	if (!rvidHeader.bmpMode) {
		for (int i = 0; i < 256; i++) {
			paletteSet[i] = false;
			palette[currentFrame][i] = 0;
		}
	}

	bool alternatePixel = false;
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
			convertedFrame16[currentFrame][i] = color;

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
					palette[currentFrame][p] = color;
					paletteSet[p] = true;
					break;
				} else if (palette[currentFrame][p] == color) {
					break;
				}
			}
			convertedFrame[currentFrame][i] = p;
		}
	}

	if (rvidHeader.bmpMode) {
		if (rvidHeader.interlaced) {
			int f = bottomField[b] ? 1 : 0;
			int x = 0;
			for(int i = 0; i < 256*rvidHeader.vRes; i++) {
				halvedFrame16[currentFrame][i] = convertedFrame16[currentFrame][(256*f)+x];
				x++;
				if (x == 256) {
					f += 2;
					x = 0;
				}
			}
			bottomField[b] = !bottomField[b];
		}
	} else {
		if (rvidHeader.interlaced) {
			int f = bottomField[b] ? 1 : 0;
			int x = 0;
			for(int i = 0; i < 256*rvidHeader.vRes; i++) {
				halvedFrame[currentFrame][i] = convertedFrame[currentFrame][(256*f)+x];
				x++;
				if (x == 256) {
					f += 2;
					x = 0;
				}
			}
			bottomField[b] = !bottomField[b];
		}
	}

	currentFrame = !currentFrame;
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

	bool reviewInformation = false;
	bool bmpModeEntered = false;

	if (rvidHeader.bmpMode == 3) {
		clear_screen();
		printf("Select the amount of colors to display on-screen.\n");
		printf("(Dithering will be applied to look like more is on-screen.)\n\n");
		printf("1: 256 (8-bit BMP, RGB565)\n- Frame Rate Limit: ");
		printf(rvidHeader.dualScreen ? "29.97" : "59.94");
		printf(" FPS\n");
		printf("- Quality varies by video frame\n");
		printf("- Recommended due to low file size and high frame rate support\n");
		printf("- Supports screen filters\n");
		printf("- Requires an installation of ImageMagick (with application directory added to system path)\n\n");
		printf("2: Unlimited (16-bit BMP, RGB555)\n- Frame Rate Limit: ");
		printf(rvidHeader.dualScreen ? "14.98" : "29.97");
		printf(" FPS\n");
		printf("- Consistent high quality\n");
		printf("- Large file size\n");
		printf("- Does not support screen filters\n");
		printf("- No additional tools needed\n\n");
		printf("3: Unlimited (16-bit BMP, RGB565)\n- Frame Rate Limit: ");
		printf(rvidHeader.dualScreen ? "14.98" : "29.97");
		printf(" FPS\n");
		printf("- Consistent high quality\n");
		printf("- Increased green color range\n");
		printf("- Large file size\n");
		printf("- Does not support screen filters\n");
		printf("- No additional tools needed\n");
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
		printf("1: 11.988 FPS\n");
		printf("2: 14.98 FPS\n");
		printf("3: 23.976 FPS\n");
		printf("4: 29.97 FPS\n");
		if (!rvidHeader.dualScreen) {
			printf("5: 47.952 FPS\n");
			printf("6: 59.94 FPS\n");
		}
		Sleep(100);

		while (1) {
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
				rvidHeader.fps = 30;
				break;
			}
			if (!rvidHeader.dualScreen) {
				if (GetKeyState('5') & 0x8000) {
					rvidHeader.fps = 48;
					break;
				}
				if (GetKeyState('6') & 0x8000) {
					rvidHeader.fps = 60;
					break;
				}
			}
			Sleep(10);
		}
		reviewInformation = true;
		rvidFpsEntered = true;
		Sleep(10);
	}

	rvidHeader.vRes = 0;
	rvidHeader.interlaced = (rvidHeader.fps > (rvidHeader.dualScreen ? 15 : 30)) ? 1 : 0;
	int fpsLimitForCompressionSupport = 24;
	if (rvidHeader.bmpMode) {
		fpsLimitForCompressionSupport /= 2;
	}
	if (rvidHeader.dualScreen) {
		fpsLimitForCompressionSupport /= 2;
	}
	int framesCompressed = 0;
	if (rvidHeader.fps <= fpsLimitForCompressionSupport) {
		framesCompressed = info.GetInt("RVID", "COMPRESSED", 2);
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

	char soundPath[256];
	sprintf(soundPath, "%s/sound.raw.pcm", framesFolder);
	bool soundFound = false;
	if (access(soundPath, F_OK) == 0) {
		rvidHeader.sampleRate = info.GetInt("RVID", "AUDIO_HZ", 0);
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
		soundFound = true;
	} else {
		rvidHeader.sampleRate = 0;
	}

	if (reviewInformation) {
		clear_screen();
		printf("Is the entered information correct?\n");
		if (bmpModeEntered) {
			printf("- Color Amount: ");
			switch (rvidHeader.bmpMode) {
				case 0:
					printf("256 (8-bit BMP, RGB565)");
					break;
				case 1:
					printf("Unlimited (16-bit BMP, RGB555)");
					break;
				case 2:
					printf("Unlimited (16-bit BMP, RGB565)");
					break;
			}
			printf("\n");
		}
		if (rvidFpsEntered) {
			printf("- Frame Rate: ");
			switch (rvidHeader.fps) {
				case 12:
					printf("11.988");
					break;
				case 15:
					printf("14.98");
					break;
				case 24:
					printf("23.976");
					break;
				case 30:
					printf("29.97");
					break;
				case 48:
					printf("47.952");
					break;
				case 60:
					printf("59.94");
					break;
			}
			printf(" FPS\n");
		}
		if (rvidCompressEntered) {
			printf("- Compressed Frames: ");
			printf(framesCompressed ? "Yes" : "No");
			printf("\n");
		}
		if (rvidSoundEntered) {
			printf("- Audio Quality: %ihz\n", rvidHeader.sampleRate);
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
		}
		if (rvidCompressEntered) {
			info.SetInt("RVID", "COMPRESSED", framesCompressed);
		}
		if (rvidSoundEntered) {
			info.SetInt("RVID", "AUDIO_HZ", rvidHeader.sampleRate);
		}
		info.SaveIniFileModified(infoIniPath);
	}

	if (!rvidHeader.bmpMode) {
		char flagPath[256];
		sprintf(flagPath, "%s/256colors", framesFolder);
		if (access(flagPath, F_OK) != 0) {
			if (access("Process Frames.bat", F_OK) != 0) {
				const u16 newLine = 0x0A0D;
				const char* line1 = "@echo Processing frames, this may take a while...";
				const char* line2 = "@cd \"";
				const char* line2End = "\"";
				const char* line3 = "@magick mogrify -ordered-dither checks,32,64,32 -colors 256 *.png";
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

	const int hRes = rvidHeader.bmpMode ? 0x200 : 0x100;

	for (int i = 0; i <= foundFrames; i++) {
		for (int b = 0; b < rvidHeader.dualScreen+1; b++) {
			frameOffsetTableSize += 4;
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
				if (rvidHeader.vRes == 0) {
					rvidHeader.vRes = (u8)height;
					if (rvidHeader.interlaced) {
						rvidHeader.vRes /= 2;
					}
				}

				if ((b == 0) && ((i % 500) == 0)) printf("%i/%i\n", i, foundFrames);

				convertFrame(b, width, image);

				bool duplicateFrameFound = false;
				if (num > 0) {
					if (rvidHeader.bmpMode) {
						if (rvidHeader.interlaced) {
							duplicateFrameFound = memcmp(halvedFrame16[0], halvedFrame16[1], 0x200*rvidHeader.vRes) == 0;
						} else {
							duplicateFrameFound = memcmp(convertedFrame16[0], convertedFrame16[1], 0x200*rvidHeader.vRes) == 0;
						}
					} else {
						duplicateFrameFound = memcmp(palette[0], palette[1], 256*2) == 0;
						if (duplicateFrameFound) {
							if (rvidHeader.interlaced) {
								duplicateFrameFound = memcmp(halvedFrame[0], halvedFrame[1], 0x100*rvidHeader.vRes) == 0;
							} else {
								duplicateFrameFound = memcmp(convertedFrame[0], convertedFrame[1], 0x100*rvidHeader.vRes) == 0;
							}
						}
					}
				}

				if (!duplicateFrameFound) {
					// Save current frame to temp file
					if (!rvidHeader.bmpMode) {
						fwrite(palette[currentFrame], 2, 256, tempFrames);
					}

					if (framesCompressed) {
						if (rvidHeader.bmpMode) {
							if (rvidHeader.interlaced) {
								compressedFrame = lzssCompress((unsigned char*)halvedFrame16[currentFrame], 0x200*rvidHeader.vRes);
							} else {
								compressedFrame = lzssCompress((unsigned char*)convertedFrame16[currentFrame], 0x200*rvidHeader.vRes);
							}
						} else {
							if (rvidHeader.interlaced) {
								compressedFrame = lzssCompress((unsigned char*)halvedFrame[currentFrame], 0x100*rvidHeader.vRes);
							} else {
								compressedFrame = lzssCompress((unsigned char*)convertedFrame[currentFrame], 0x100*rvidHeader.vRes);
							}
						}
					}

					if (!framesCompressed || (frameFileSize >= hRes*rvidHeader.vRes)) {
						// Store uncompressed frame if compressed frame is exactly the same size or larger, or if compression is disabled
						frameFileSize = hRes*rvidHeader.vRes;
						if (rvidHeader.bmpMode) {
							fwrite(rvidHeader.interlaced ? halvedFrame16[currentFrame] : convertedFrame16[currentFrame], 1, frameFileSize, tempFrames);
						} else {
							fwrite(rvidHeader.interlaced ? halvedFrame[currentFrame] : convertedFrame[currentFrame], 1, frameFileSize, tempFrames);
						}
					} else {
						fwrite(compressedFrame, 1, frameFileSize, tempFrames);
					}
				}

				if (num > 1) {
					frameOffsetTable[num] = frameOffsetTable[num-1];
				}
				if ((num > 0) && !duplicateFrameFound) {
					if (!rvidHeader.bmpMode) {
						frameOffsetTable[num] += 0x200;
					}
					frameOffsetTable[num] += previousFrameFileSize;
				}
				if (framesCompressed) {
					if (rvidHeader.bmpMode) {
						compressedFrameSizeTable32[num] = frameFileSize;
					} else {
						compressedFrameSizeTable16[num] = frameFileSize;
					}
				}
				previousFrameFileSize = frameFileSize;
				if (!duplicateFrameFound) {
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

	for (int i = 0; i <= foundFrames; i++) {
		for (int b = 0; b < rvidHeader.dualScreen+1; b++) {
			const int num = rvidHeader.dualScreen ? (i*2)+b : i;
			frameOffsetTable[num] += 0x200+frameOffsetTableSize+compressedFrameSizeTableSize;
		}
	}

	if (framesCompressed) {
		rvidHeader.compressedFrameSizeTableOffset = 0x200+frameOffsetTableSize;
	}
	rvidHeader.soundOffset = soundFound ? 0x200+frameOffsetTableSize+compressedFrameSizeTableSize+tempFramesSize : 0;

	FILE* videoOutput = fopen("output.rvid", "wb");
	if (!videoOutput) {
		clear_screen();
		printf("Failed to create output.rvid\n");
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

	// Write header
	memcpy(headerToFile, &rvidHeader, sizeof(rvidHeaderInfo));
	fwrite(headerToFile, 1, 0x200, videoOutput);

	fwrite(frameOffsetTable, 1, frameOffsetTableSize, videoOutput);
	delete[] frameOffsetTable;

	clear_screen();
	printf("Adding frames...\n");

	if (framesCompressed) {
		if (rvidHeader.bmpMode) {
			fwrite(compressedFrameSizeTable32, 1, compressedFrameSizeTableSize, videoOutput);
			delete[] compressedFrameSizeTable32;
		} else {
			fwrite(compressedFrameSizeTable16, 1, compressedFrameSizeTableSize, videoOutput);
			delete[] compressedFrameSizeTable16;
		}
	}

	u32 fsize = tempFramesSize;
	u32 offset = 0;
	int numr = 0;

	tempFrames = fopen("tempFrames.bin", "rb");
	while (1)
	{
		// Add frames to .rvid file
		numr = fread(fileBuffer, 1, sizeof(fileBuffer), tempFrames);
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
		fwrite(fileBuffer, 1, numr, videoOutput);
		offset += sizeof(fileBuffer);

		if (offset >= fsize) {
			break;
		}
	}
	fclose(tempFrames);

	if (soundFound) {
		clear_screen();
		printf("Adding sound...\n");

		u32 fsize = getFileSize(soundPath);
		u32 offset = 0;
		int numr;

		FILE* soundFile = fopen(soundPath, "rb");
		while (1)
		{
			// Add sound to .rvid file
			numr = fread(fileBuffer, 1, sizeof(fileBuffer), soundFile);
			fwrite(fileBuffer, 1, numr, videoOutput);
			offset += sizeof(fileBuffer);

			if (offset > fsize) {
				break;
			}
		}
		fclose(soundFile);
	}

	fclose(videoOutput);

	remove("tempFrames.bin");

	clear_screen();
	printf("Done!\n");

	return 0;
}
