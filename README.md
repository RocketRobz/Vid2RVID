# Vid2RVID
Video to Rocket Video (`.rvid`) converter

Requires [ImageMagick](https://imagemagick.org/) installed with

    - Windows: application directory added to system path
    - Linux: `magick` command available in PATH

Converted video files can be run with [Rocket Video Player](https://github.com/RocketRobz/RocketVideoPlayer)

# Usage for Linux

Open a terminal, then run:

```cmd
./Vid2RVID pathof/rvidFrames_folder
```

replacing `pathof/rvidFrames_folder` with the actual folder for the extracted frames folder.

# Compiling

Make sure you have the basic development software installed on your Windows or Linux environment, then run

## Windows

Double-click on `compile.bat`

## Linux

```
cd pc
bash compile.sh
```

# Credits
- [devkitPro](https://github.com/devkitPro): devkitPro, devkitARM, libnds, and libfat. (DS version)
- [Drenn](https://github.com/Drenn1): GameYob's `.bmp` renderer for (deprecated) DS version.
- [Gericom](https://github.com/Gericom): LZ77 compressor code from EFE/EveryFileExplorer.
- [Inky1003](https://github.com/Inky1003): Code used from [Linux port](https://github.com/Inky1003/Vid2RVID) of v1.6
- [lvandeve](https://github.com/lvandeve): [lodepng](https://github.com/lvandeve/lodepng)
