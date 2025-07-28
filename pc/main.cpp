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

uint8_t convertedFrame[256*192];
uint8_t halvedFrame[256*96];
unsigned char* compressedFrame;

char fileBuffer[0x100000] = {0};
uint32_t compressedFrameSizeTableSize = 0;
uint32_t compressedFramesSize = 0;
uint32_t soundSize = 0;

uint8_t headerToFile[0x200] = {0};

uint32_t getFileSize(const char *fileName)
{
	FILE* fp = fopen(fileName, "rb");
	uint32_t fsize = 0;
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
	uint32_t formatString;  	    // "RVID" string
	uint32_t ver;			        // File format version
	uint32_t frames;			    // Number of frames
	uint8_t fps;				    // Frames per second
	uint8_t vRes;			        // Vertical resolution
	uint8_t interlaced;		        // Is interlaced
	uint8_t hasSound;			    // Has sound/audio
	uint16_t sampleRate;		    // Audio sample rate
	uint16_t framesCompressed;	    // Frames are compressed
	uint32_t framesOffset;		    // Offset of first frame
	uint32_t soundOffset;		    // Offset of sound stream
} rvidHeaderInfo;

rvidHeaderInfo rvidHeader;

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

int main(int argc, char **argv) {

	printf(titleText);
	printf("\n");
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

	CIniFile info( "rvidFrames/info.ini" );

	if ((info.GetInt("RVID", "HAS_SOUND", 1) == 1) && (access("rvidFrames/sound.raw.pcm", F_OK) == 0)) {
		rvidHeader.sampleRate = info.GetInt("RVID", "AUDIO_HZ", 0);
		if (rvidHeader.sampleRate > 0) {
			rvidHeader.hasSound = 1;
		} else {
			clear_screen();
			printf("Sound file found!\n");
			printf("\n");
			printf("What is the sample rate?\n");
			printf("0: Exclude sound\n");
			printf("1: 8000hz\n");
			printf("2: 11025hz\n");
			printf("3: 16000hz\n");
			printf("4: 22050hz\n");
			printf("5: 32000hz\n");

			while (1) {
				if (GetKeyState('0') & 0x8000) {
					rvidHeader.hasSound = 0;
					break;
				}
				if (GetKeyState('1') & 0x8000) {
					rvidHeader.sampleRate = 8000;
					rvidHeader.hasSound = 1;
					break;
				}
				if (GetKeyState('2') & 0x8000) {
					rvidHeader.sampleRate = 11025;
					rvidHeader.hasSound = 1;
					break;
				}
				if (GetKeyState('3') & 0x8000) {
					rvidHeader.sampleRate = 16000;
					rvidHeader.hasSound = 1;
					break;
				}
				if (GetKeyState('4') & 0x8000) {
					rvidHeader.sampleRate = 22050;
					rvidHeader.hasSound = 1;
					break;
				}
				if (GetKeyState('5') & 0x8000) {
					rvidHeader.sampleRate = 32000;
					rvidHeader.hasSound = 1;
					break;
				}
				Sleep(10);
			}
		}
	}

	clear_screen();
	printf("Getting number of frames...\n");

	char framePath[256];
	int foundFrames = info.GetInt("RVID", "FRAMES", -1);

	if (foundFrames == -1) {
		while (1) {
			foundFrames++;
			snprintf(framePath, sizeof(framePath), "rvidFrames/frame%i.png", foundFrames);
			if (access(framePath, F_OK) != 0) break;
		}
		foundFrames--;
	}

	rvidHeader.formatString = 0x44495652;	// "RVID"
	rvidHeader.ver = rvidVer;
	rvidHeader.frames = foundFrames+1;
	rvidHeader.fps = info.GetInt("RVID", "FPS", 24);
	rvidHeader.vRes = 0;
	rvidHeader.interlaced = (rvidHeader.fps > 30) ? 1 : 0;
	if (rvidHeader.fps > 25) {
		rvidHeader.framesCompressed = 0;
	} else {
		rvidHeader.framesCompressed = info.GetInt("RVID", "COMPRESSED", 2);
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

	if (rvidHeader.framesCompressed == 2) {
		clear_screen();
		printf("Compress the video frames?\n");
		printf("Video quality will not be affected.\n");
		printf("Recommended if your video is 25FPS or less.\n");
		printf("Depending on how may frames you have, this may take a while.\n");
		printf("\n");
		printf("Y: Yes\n");
		printf("N: No\n");

		while (1) {
			if (GetKeyState('Y') & 0x8000) {
				rvidHeader.framesCompressed = 1;
				break;
			}
			if (GetKeyState('N') & 0x8000) {
				rvidHeader.framesCompressed = 0;
				break;
			}
			Sleep(10);
		}
	}

	if (access("rvidFrames/256colors", F_OK) != 0) {
		if (access("Process Frames.bat", F_OK) != 0) {
			const uint16_t newLine = 0x0A0D;
			const char* line1 = "@echo Processing frames, this may take a while...";
			const char* line2 = "@cd rvidFrames";
			const char* line3 = "@magick mogrify -ordered-dither o8x8,32,64,32 -colors 256 *.png";
			const char* line4 = "@mkdir 256colors";
			const char* line5 = "@echo Done!";
			const char* line6 = "@pause";

			FILE* batFile = fopen("Process Frames.bat", "wb");
			fwrite(line1, 1, strlen(line1), batFile);
			fwrite(&newLine, 2, 1, batFile);
			fwrite(line2, 1, strlen(line2), batFile);
			fwrite(&newLine, 2, 1, batFile);
			fwrite(line3, 1, strlen(line3), batFile);
			fwrite(&newLine, 2, 1, batFile);
			fwrite(line4, 1, strlen(line4), batFile);
			fwrite(&newLine, 2, 1, batFile);
			fwrite(line5, 1, strlen(line5), batFile);
			fwrite(&newLine, 2, 1, batFile);
			fwrite(line6, 1, strlen(line6), batFile);
			fclose(batFile);
		}

		while (access("rvidFrames/256colors", F_OK) != 0) {
			clear_screen();
			printf("Ensure ImageMagick is installed (with application directory added to system path),\n");
			printf("then open \"Process Frames.bat\".\n\n");
			printf("When the processing is done, press ENTER.\n");

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

	FILE* frameInput;

	FILE* compressedFrameSizeTable;
	FILE* compressedFrames;
	if (rvidHeader.framesCompressed == 1) {
		clear_screen();
		printf("Compressing...\n");

		compressedFrameSizeTable = fopen("tempTable.bin", "wb");
		compressedFrames = fopen("tempFrames.bin", "wb");
		for (int i = 0; i <= foundFrames; i++) {
			snprintf(framePath, sizeof(framePath), "rvidFrames/frame%i.png", i);
			frameInput = fopen(framePath, "rb");
			if (frameInput) {
				fclose(frameInput);

				std::vector<unsigned char> image;
				unsigned width, height;
				lodepng::decode(image, width, height, framePath);
				if (rvidHeader.vRes == 0) {
					rvidHeader.vRes = (uint8_t)height;
					if (rvidHeader.interlaced) {
						rvidHeader.vRes /= 2;
					}
				}

				bool paletteSet[256] = {false};
				uint16_t palette[256] = {0};
				for(unsigned i=0;i<image.size()/4;i++) {
					const uint16_t green = (image[(i*4)+1] >> 2) << 5;
					uint16_t color = image[i*4] >> 3 | (image[(i*4)+2] >> 3) << 10;
					if (green & BIT(5)) {
						color |= BIT(15);
					}
					for (int gBit = 6; gBit <= 10; gBit++) {
						if (green & BIT(gBit)) {
							color |= BIT(gBit-1);
						}
					}

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

				if (rvidHeader.interlaced) {
					static bool bottomField = false;
					int f = bottomField ? 1 : 0;
					int x = 0;
					for(int i = 0; i < 256*rvidHeader.vRes; i++) {
						halvedFrame[i] = convertedFrame[(256*f)+x];
						x++;
						if (x == 256) {
							f += 2;
							x = 0;
						}
					}
					bottomField = !bottomField;

					compressedFrame = lzssCompress((unsigned char*)halvedFrame, 0x100*rvidHeader.vRes);
				} else {
					compressedFrame = lzssCompress((unsigned char*)convertedFrame, 0x100*rvidHeader.vRes);
				}

				if ((i % 500) == 0) printf("%i/%i\n", i, foundFrames);

				// Save current frame to temp file
				fwrite(palette, 2, 256, compressedFrames);
				fwrite(compressedFrame, 1, compressedDataSize, compressedFrames);
				fwrite(&compressedDataSize, 4, 1, compressedFrameSizeTable);
				compressedFrameSizeTableSize += 4;
				compressedFramesSize += 0x200;
				compressedFramesSize += compressedDataSize;
			} else {
				break;
			}
		}
		fclose(compressedFrames);
		fclose(compressedFrameSizeTable);
		rvidHeader.framesOffset = 0x200+compressedFrameSizeTableSize;
		rvidHeader.soundOffset = 0x200+compressedFrameSizeTableSize+compressedFramesSize;
	} else {
		snprintf(framePath, sizeof(framePath), "rvidFrames/frame%i.png", 0);
		frameInput = fopen(framePath, "rb");
		if (frameInput) {
			fclose(frameInput);

			std::vector<unsigned char> image;
			unsigned width, height;
			lodepng::decode(image, width, height, framePath);
			rvidHeader.vRes = (uint8_t)height;
			if (rvidHeader.interlaced) {
				rvidHeader.vRes /= 2;
			}
		}

		rvidHeader.framesOffset = 0x200;
		rvidHeader.soundOffset = 0x200+((0x200+(0x100*rvidHeader.vRes))*rvidHeader.frames);
	}

	FILE* videoOutput = fopen("new.rvid", "wb");

	// Write header
	memcpy(headerToFile, &rvidHeader, sizeof(rvidHeaderInfo));
	fwrite(headerToFile, 1, 0x200, videoOutput);

	clear_screen();
	printf("Converting...\n");

	if (rvidHeader.framesCompressed == 1) {
		uint32_t fsize = compressedFrameSizeTableSize;
		uint32_t offset = 0;
		int numr = 0;

		compressedFrameSizeTable = fopen("tempTable.bin", "rb");
		while (1)
		{
			// Add size table to .rvid file
			numr = fread(fileBuffer, 1, sizeof(fileBuffer), compressedFrameSizeTable);
			fwrite(fileBuffer, 1, numr, videoOutput);
			offset += sizeof(fileBuffer);

			if (offset > fsize) {
				break;
			}
		}
		fclose(compressedFrameSizeTable);

		fsize = compressedFramesSize;
		offset = 0;
		numr = 0;

		compressedFrames = fopen("tempFrames.bin", "rb");
		while (1)
		{
			// Add compressed frames to .rvid file
			numr = fread(fileBuffer, 1, sizeof(fileBuffer), compressedFrames);
			fwrite(fileBuffer, 1, numr, videoOutput);
			offset += sizeof(fileBuffer);

			if (offset > fsize) {
				break;
			}
		}
		fclose(compressedFrames);
	} else for (int i = 0; i <= foundFrames; i++) {
		snprintf(framePath, sizeof(framePath), "rvidFrames/frame%i.png", i);
		frameInput = fopen(framePath, "rb");
		if (frameInput) {
			fclose(frameInput);

			std::vector<unsigned char> image;
			unsigned width, height;
			lodepng::decode(image, width, height, framePath);

			bool paletteSet[256] = {false};
			uint16_t palette[256] = {0};
			for(unsigned i=0;i<image.size()/4;i++) {
				const uint16_t green = (image[(i*4)+1] >> 2) << 5;
				uint16_t color = image[i*4] >> 3 | (image[(i*4)+2] >> 3) << 10;
				if (green & BIT(5)) {
					color |= BIT(15);
				}
				for (int gBit = 6; gBit <= 10; gBit++) {
					if (green & BIT(gBit)) {
						color |= BIT(gBit-1);
					}
				}

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

			if (rvidHeader.interlaced) {
				static bool bottomField = false;
				int f = bottomField ? 1 : 0;
				int x = 0;
				for(int i = 0; i < 256*rvidHeader.vRes; i++) {
					halvedFrame[i] = convertedFrame[(256*f)+x];
					x++;
					if (x == 256) {
						f += 2;
						x = 0;
					}
				}
				bottomField = !bottomField;
			}

			if ((i % 500) == 0) printf("%i/%i\n", i, foundFrames);

			// Save current frame to a file
			fwrite(palette, 2, 256, videoOutput);
			fwrite(rvidHeader.interlaced ? halvedFrame : convertedFrame, 1, 0x100*rvidHeader.vRes, videoOutput);
		} else {
			break;
		}
	}

	if (rvidHeader.hasSound == 1) {
		clear_screen();
		printf("Adding sound...\n");

		uint32_t fsize = getFileSize("rvidFrames/sound.raw.pcm");
		uint32_t offset = 0;
		int numr;

		FILE* soundFile = fopen("rvidFrames/sound.raw.pcm", "rb");
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

	if (rvidHeader.framesCompressed == 1) {
		remove("tempFrames.bin");
		remove("tempTable.bin");
	}

	clear_screen();
	printf("Done!\n");

	return 0;
}
