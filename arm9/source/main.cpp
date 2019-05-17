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

#include "inifile.h"

u16 loadedFrame[256*192];
u16 convertedFrame[256*192];

u8 headerToFile[0x200] = {0};

typedef struct rvidHeaderInfo {
	u32 formatString;	// "RVID" string
	u32 ver;			// File format version
	u32 frames;			// Number of frames
	u8 fps;				// Frames per second
	u8 vRes;			// Vertical resolution
} rvidHeaderInfo;

rvidHeaderInfo rvidHeader;

#define titleText "Vid2RVID, by RocketRobz\n"

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

	printf ("\x1b[2;0H");
	printf("Getting number of frames...");

	char framePath[256];
	int foundFrames = -1;

	while (1) {
		foundFrames++;
		snprintf(framePath, sizeof(framePath), "/rvidFrames/frame%i.bmp", foundFrames);
		if (access(framePath, F_OK) != 0) break;
	}

	printf ("\x1b[2;0H");
	printf("Converting...              ");

	CIniFile info( "/rvidFrames/info.ini" );
	rvidHeader.formatString = 0x44495652;	// "RVID"
	rvidHeader.ver = 1;
	rvidHeader.frames = foundFrames;
	rvidHeader.fps = info.GetInt("RVID", "FPS", 24);
	rvidHeader.vRes = info.GetInt("RVID", "V_RES", 192);

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
		snprintf(framePath, sizeof(framePath), "/rvidFrames/frame%i.bmp", i);
		frameInput = fopen(framePath, "rb");
		if (frameInput) {
			// Load frame
			fseek(frameInput, 0xe, SEEK_SET);
			u8 pixelStart = (u8)fgetc(frameInput) + 0xe;
			fseek(frameInput, pixelStart, SEEK_SET);
			fread(loadedFrame, 2, 0x200*rvidHeader.vRes, frameInput);
			u16* src = loadedFrame;

			// Convert frame
			int x = 0;
			int y = rvidHeader.vRes-1;
			for (int i=0; i<256*rvidHeader.vRes; i++) {
				if (x >= 256) {
					x = 0;
					y--;
				}
				u16 val = *(src++);
				convertedFrame[y*256+x] = ((val>>10)&31) | (val&31<<5) | (val&31)<<10 | BIT(15);
				x++;
			}

			// Display converted frame
			dmaCopy(convertedFrame, (u16*)BG_GFX_SUB+(256*videoYpos), 0x200*rvidHeader.vRes);

			printf ("\x1b[4;0H");
			printf("%i/%i\n", i, foundFrames);

			// Save current frame to a file
			fwrite(convertedFrame, 1, 0x200*rvidHeader.vRes, videoOutput);

			fclose(frameInput);
		} else {
			break;
		}
	}

	fclose(videoOutput);

	consoleClear();
	printf(titleText);
	printf("\n");
	printf("Done!\n");

	//for (int i = 0; i < 60*3; i++) {
	while (1) {
		swiWaitForVBlank();
	}

	return 0;
}

