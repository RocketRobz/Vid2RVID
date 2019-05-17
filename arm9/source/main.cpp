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

u16 loadedFrame[256*192];
u16 convertedFrame[256*192];

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
	printf("A: Convert\n");

	do
	{
		scanKeys();
		pressed = keysDown();
		swiWaitForVBlank();
	}
	while (!(pressed & KEY_A));

	consoleClear();
	printf(titleText);
	printf("\n");
	printf("Converting...\n");

	char framePath[256];
	int foundFrames = -1;
	FILE* frameInput;
	FILE* videoOutput = fopen("/new.rvid", "wb");

	while (1) {
		foundFrames++;
		snprintf(framePath, sizeof(framePath), "/rvidFrames/frame%i.bmp", foundFrames);
		if (access(framePath, F_OK) != 0) break;

		frameInput = fopen(framePath, "rb");
		if (frameInput) {
			// Load frame
			fseek(frameInput, 0xe, SEEK_SET);
			u8 pixelStart = (u8)fgetc(frameInput) + 0xe;
			fseek(frameInput, pixelStart, SEEK_SET);
			fread(loadedFrame, 2, 0x18000, frameInput);
			u16* src = loadedFrame;

			// Convert frame
			int x = 0;
			int y = 191;
			for (int i=0; i<256*192; i++) {
				if (x >= 256) {
					x = 0;
					y--;
				}
				u16 val = *(src++);
				convertedFrame[y*256+x] = ((val>>10)&31) | (val&31<<5) | (val&31)<<10 | BIT(15);
				x++;
			}

			// Display converted frame
			dmaCopy(convertedFrame, BG_GFX_SUB, 0x18000);

			// Save current frame to a file
			fwrite(convertedFrame, 1, 0x18000, videoOutput);

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

