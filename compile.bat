mkdir bin
cd source
g++ -O2 sha1.c inifile.cpp graphics/lodepng.cpp lz77.cpp main.cpp -s -static -o ../bin/Vid2RVID
