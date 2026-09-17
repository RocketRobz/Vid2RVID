#!/usr/bin/env bash
[[ ! -d bin ]] && mkdir bin
cd source
# -O0: an -O2 build aborts with a _FORTIFY_SOURCE buffer overflow during dual-screen encoding.
# No -static: it fails to link on Linux.
g++ -O0 sha1.c inifile.cpp graphics/lodepng.cpp lz77.cpp main.cpp -s -o ../bin/Vid2RVID
