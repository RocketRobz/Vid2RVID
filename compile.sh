#!/usr/bin/env bash
[[ ! -d bin ]] && mkdir bin
cd source
# No -static: it fails to link on Linux.
g++ -O2 sha1.c inifile.cpp graphics/lodepng.cpp lz77.cpp main.cpp -s -o ../bin/Vid2RVID
