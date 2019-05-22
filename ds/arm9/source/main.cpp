/*
    NitroHax -- Cheat tool for the Nintendo DS
    Copyright (C) 2008  Michael "Chishm" Chisholm

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <nds.h>
#include <fat.h>

#include <stdio.h>
#include <string.h>

#include "graphics/lodepng.h"
#include "inifile.h"

u16 convertedFrame[256*192];

char soundBuffer[0x8000] = {0};
u32 soundSize = 0;

u8 headerToFile[0x200] = {0};

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
	u32 formatString;  	// "RVID" string
	u32 ver;			    // File format version
	u32 frames;			// Number of frames
	u8 fps;				// Frames per second
	u8 vRes;			    // Vertical resolution
	u8 hasSound;			// Has sound/audio
	u8 reserved;
	u16 sampleRate;		// Audio sample rate
} rvidHeaderInfo;

rvidHeaderInfo rvidHeader;

#define titleText "Vid2RVID, by RocketRobz\n"

int hourMark = 0;
int minuteMark = 0;
int secondMark = 0;
int frameMark = 0;
bool timeElapseUpdate = true;

void vBlankHandler(void) {
	if (frameMark == 60) {
		timeElapseUpdate = true;
		frameMark = 0;
		secondMark++;
		if (secondMark == 60) {
			secondMark = 0;
			minuteMark++;
			if (minuteMark == 60) {
				minuteMark = 0;
				hourMark++;
			}
		}
	}
	frameMark++;

	if (timeElapseUpdate) {
		printf("\x1b[10;0H");
		printf("Time elapsed: ");
		if (hourMark < 10) {
			printf("0%i", hourMark);
		} else {
			printf("%i", hourMark);
		}
		printf(":");
		if (minuteMark < 10) {
			printf("0%i", minuteMark);
		} else {
			printf("%i", minuteMark);
		}
		printf(":");
		if (secondMark < 10) {
			printf("0%i", secondMark);
		} else {
			printf("%i", secondMark);
		}
		timeElapseUpdate = false;
	}
}

int main(int argc, char **argv) {

	if (!fatInitDefault()) {
		consoleDemoInit();
		printf("fatInitDefault failed!");
	}

	lcdMainOnBottom();

	videoSetMode(MODE_0_2D);
	vramSetBankG(VRAM_G_MAIN_BG);
	consoleInit(NULL, 0, BgType_Text4bpp, BgSize_T_256x256, 15, 0, true, true);

	videoSetModeSub(MODE_3_2D | DISPLAY_BG3_ACTIVE);
	vramSetBankC(VRAM_C_SUB_BG_0x06200000);

	REG_BG3CNT_SUB = BG_MAP_BASE(0) | BG_BMP16_256x256 | BG_PRIORITY(0);
	REG_BG3X_SUB = 0;
	REG_BG3Y_SUB = 0;
	REG_BG3PA_SUB = 1<<8;
	REG_BG3PB_SUB = 0;
	REG_BG3PC_SUB = 0;
	REG_BG3PD_SUB = 1<<8;

	int pressed = 0;

	printf(titleText);
	printf("\n");
	printf("A: Convert");

	do
	{
		scanKeys();
		pressed = keysDown();
		swiWaitForVBlank();
	}
	while (!(pressed & KEY_A));

    if (access("/rvidFrames/sound.raw.pcm", F_OK) == 0) {
		int cursorPosition = 0;
		printf ("\x1b[2;0H");
        printf("Sound file found!\n");
        printf("\n");
        printf("What is the sample rate?\n");
        printf("> Exclude sound\n");
        printf("  8000hz\n");
        printf("  11025hz\n");
        printf("  16000hz\n");

        while (1) {
			scanKeys();
			pressed = keysDown();
			if (pressed & KEY_A) {
				break;
			}
			if (pressed & KEY_UP) {
				printf("\x1b[%i;0H", cursorPosition+5);
				printf(" ");
				cursorPosition--;
				if (cursorPosition < 0) cursorPosition = 3;
				printf("\x1b[%i;0H", cursorPosition+5);
				printf(">");
			}
			if (pressed & KEY_DOWN) {
				printf("\x1b[%i;0H", cursorPosition+5);
				printf(" ");
				cursorPosition++;
				if (cursorPosition > 3) cursorPosition = 0;
				printf("\x1b[%i;0H", cursorPosition+5);
				printf(">");
			}
			swiWaitForVBlank();
        }
        if (cursorPosition == 0) {
            rvidHeader.hasSound = 0;
        }
        if (cursorPosition == 1) {
            rvidHeader.sampleRate = 8000;
            rvidHeader.hasSound = 1;
        }
        if (cursorPosition == 2) {
            rvidHeader.sampleRate = 11025;
            rvidHeader.hasSound = 1;
        }
        if (cursorPosition == 3) {
            rvidHeader.sampleRate = 16000;
            rvidHeader.hasSound = 1;
        }
    }

	consoleClear();

	irqSet(IRQ_VBLANK, vBlankHandler);
	irqEnable(IRQ_VBLANK);

	printf(titleText);
	printf("\n");
	printf("Getting number of frames...");

	CIniFile info( "/rvidFrames/info.ini" );

	char framePath[256];
	int foundFrames = info.GetInt("RVID", "FRAMES", -1);

	if (foundFrames == -1) {
		while (1) {
			foundFrames++;
			snprintf(framePath, sizeof(framePath), "/rvidFrames/frame%i.png", foundFrames);
			if (access(framePath, F_OK) != 0) break;
		}
	}

	rvidHeader.formatString = 0x44495652;	// "RVID"
	rvidHeader.ver = 1;
	rvidHeader.frames = foundFrames;
	rvidHeader.fps = info.GetInt("RVID", "FPS", 24);
	rvidHeader.vRes = info.GetInt("RVID", "V_RES", 192);

	printf ("\x1b[2;0H");
	printf("Converting...              ");

	FILE* frameInput;
	FILE* videoOutput = fopen("/new.rvid", "wb");
	
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
		snprintf(framePath, sizeof(framePath), "/rvidFrames/frame%i.png", i);
		frameInput = fopen(framePath, "rb");
		if (frameInput) {
			fclose(frameInput);

			std::vector<unsigned char> image;
			unsigned width, height;
			lodepng::decode(image, width, height, framePath);

			for(unsigned i=0;i<image.size()/4;i++) {
				convertedFrame[i] = image[i*4]>>3 | (image[(i*4)+1]>>3)<<5 | (image[(i*4)+2]>>3)<<10 | BIT(15);
			}

			// Display converted frame
			dmaCopyAsynch(convertedFrame, (u16*)BG_GFX_SUB+(256*videoYpos), 0x200*rvidHeader.vRes);

			while (timeElapseUpdate) {
				swiWaitForVBlank();
			}
			printf ("\x1b[4;0H");
			printf("%i/%i\n", i, foundFrames);

			// Save current frame to a file
			fwrite(convertedFrame, 1, 0x200*rvidHeader.vRes, videoOutput);
		} else {
			break;
		}
	}

	if (rvidHeader.hasSound == 1) {
		printf ("\x1b[2;0H");
        printf("Adding sound...");
		printf ("\x1b[4;0H");
		printf("                           ");

        off_t fsize = getFileSize("/rvidFrames/sound.raw.pcm");
        off_t offset = 0;
        int numr;

        FILE* soundFile = fopen("/rvidFrames/sound.raw.pcm", "rb");
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

	irqDisable(IRQ_VBLANK);

	printf ("\x1b[2;0H");
	printf("Done!                      ");
	printf ("\x1b[4;0H");
	printf("                           ");

	//for (int i = 0; i < 60*3; i++) {
	while (1) {
		swiWaitForVBlank();
	}

	return 0;
}

