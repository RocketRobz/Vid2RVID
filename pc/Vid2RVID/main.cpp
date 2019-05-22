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

uint16_t convertedFrame[256*192];

uint8_t headerToFile[0x200] = {0};

typedef struct rvidHeaderInfo {
	uint32_t formatString;	// "RVID" string
	uint32_t ver;			// File format version
	uint32_t frames;			// Number of frames
	uint8_t fps;				// Frames per second
	uint8_t vRes;			// Vertical resolution
} rvidHeaderInfo;

rvidHeaderInfo rvidHeader;

#define titleText "Vid2RVID, by RocketRobz\n"

int main(int argc, char **argv) {

	printf(titleText);
	printf("\n");
	printf("A: Convert\n");

	while (!(GetKeyState('A') & 0x8000));

	printf ("\x1b[2;0H");
	printf("Getting number of frames...\n");

	CIniFile info( "rvidFrames/info.ini" );

	char framePath[256];
	int foundFrames = info.GetInt("RVID", "FRAMES", -1);

	if (foundFrames == -1) {
		while (1) {
			foundFrames++;
			snprintf(framePath, sizeof(framePath), "/rvidFrames/frame%i.bmp", foundFrames);
			if (access(framePath, F_OK) != 0) break;
		}
	}

	printf("Converting...\n");

	rvidHeader.formatString = 0x44495652;	// "RVID"
	rvidHeader.ver = 1;
	rvidHeader.frames = foundFrames;
	rvidHeader.fps = info.GetInt("RVID", "FPS", 24);
	rvidHeader.vRes = info.GetInt("RVID", "V_RES", 192);

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

	fclose(videoOutput);

	printf("Done!\n");

	return 0;
}

