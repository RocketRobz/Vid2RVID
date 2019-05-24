#include <stdio.h>
#include <stdlib.h>         //built with codeblocks v10.05 and mingw
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

uint16_t convertedFrame[256*192];

char soundBuffer[0x100000] = {0};
uint32_t soundSize = 0;

uint8_t headerToFile[0x200] = {0};

off_t getFileSize(const char *fileName)
{
    FILE* fp = fopen(fileName, "rb");
    off_t fsize = 0;
    if (fp) {
        fseek(fp, 0, SEEK_END);
        fsize = ftell(fp);			// Get source file's size
		fseek(fp, 0, SEEK_SET);
	}
	fclose(fp);

	return fsize;
}

typedef struct rvidHeaderInfo {
	uint32_t formatString;  	// "RVID" string
	uint32_t ver;			    // File format version
	uint32_t frames;			// Number of frames
	uint8_t fps;				// Frames per second
	uint8_t vRes;			    // Vertical resolution
	uint8_t interlaced;		    // Is interlaced
	uint8_t hasSound;			// Has sound/audio
	uint16_t sampleRate;		// Audio sample rate
} rvidHeaderInfo;

rvidHeaderInfo rvidHeader;

#define titleText "Vid2RVID, by RocketRobz\n"

int main(int argc, char **argv) {

	printf(titleText);
	printf("\n");
	printf("A: Convert\n");

	while (!(GetKeyState('A') & 0x8000));

    if (access("rvidFrames/sound.raw.pcm", F_OK) == 0) {
        clear_screen();
        printf("Sound file found!\n");
        printf("\n");
        printf("What is the sample rate?\n");
        printf("0: Exclude sound\n");
        printf("1: 8000hz\n");
        printf("2: 11025hz\n");
        printf("3: 16000hz\n");

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
        }
    }

	clear_screen();
	printf("Getting number of frames...\n");

	CIniFile info( "rvidFrames/info.ini" );

	char framePath[256];
	int foundFrames = info.GetInt("RVID", "FRAMES", -1);

	if (foundFrames == -1) {
		while (1) {
			foundFrames++;
			snprintf(framePath, sizeof(framePath), "rvidFrames/frame%i.png", foundFrames);
			if (access(framePath, F_OK) != 0) break;
		}
	}

	rvidHeader.formatString = 0x44495652;	// "RVID"
	rvidHeader.ver = 2;
	rvidHeader.frames = foundFrames+1;
	rvidHeader.fps = info.GetInt("RVID", "FPS", 24);
	rvidHeader.vRes = info.GetInt("RVID", "V_RES", 192);
	rvidHeader.interlaced = info.GetInt("RVID", "INTERLACED", 0);

	clear_screen();
	printf("Converting...\n");

	FILE* frameInput;
	FILE* videoOutput = fopen("new.rvid", "wb");

	// Write header
	memcpy(headerToFile, &rvidHeader, sizeof(rvidHeaderInfo));
	fwrite(headerToFile, 1, 0x200, videoOutput);

	int videoYpos = 0;
	if (rvidHeader.vRes <= 190) {
		// Adjust video positioning
		for (int i = rvidHeader.vRes; i < 192; i += 2) {
			videoYpos++;
		}
	}

	for (int i = 0; i <= foundFrames; i++) {
		snprintf(framePath, sizeof(framePath), "rvidFrames/frame%i.png", i);
		frameInput = fopen(framePath, "rb");
		if (frameInput) {
			fclose(frameInput);

			std::vector<unsigned char> image;
			unsigned width, height;
			lodepng::decode(image, width, height, framePath);

			for(unsigned i=0;i<image.size()/4;i++) {
				convertedFrame[i] = image[i*4]>>3 | (image[(i*4)+1]>>3)<<5 | (image[(i*4)+2]>>3)<<10 | BIT(15);
			}

			printf("%i/%i\n", i, foundFrames);

			// Save current frame to a file
			fwrite(convertedFrame, 1, 0x200*rvidHeader.vRes, videoOutput);
		} else {
			break;
		}
	}

	if (rvidHeader.hasSound == 1) {
        clear_screen();
        printf("Adding sound...\n");

        off_t fsize = getFileSize("rvidFrames/sound.raw.pcm");
        off_t offset = 0;
        int numr;

        FILE* soundFile = fopen("rvidFrames/sound.raw.pcm", "rb");
        while (1)
        {
            // Add sound to .rvid file
            numr = fread(soundBuffer, 1, sizeof(soundBuffer), soundFile);
            fwrite(soundBuffer, 1, numr, videoOutput);
            offset += sizeof(soundBuffer);

            if (offset > fsize) {
                break;
            }
        }
        fclose(soundFile);
    }

    fclose(videoOutput);

    clear_screen();
	printf("Done!\n");

	return 0;
}

