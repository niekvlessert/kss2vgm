# KSS2VGM

KSS2VGM is a libkss-based VGM (.vgm) file generator for various MSX music formats.
Supported input formats are .kss, .mgs, .bgm, .opx, .mpk, .mbm.

Note that KSS2VGM is still an experimental, minimum command-line options are available
and output .vgm file is not optimized. Y8950 adpcm is not supported yet.

# TL;DR

The following command converts the 8th song of favorite.kss to out.vgm of 120 seconds.

```
$ ./kss2vgm -p120 -s8 -oout.vgm favorite.kss
```

# How to build

```
$ git clone --recursive https://github.com/digital-sound-antiques/kss2vgm.git
$ cd kss2vgm
$ mkdir build
$ cd build
$ cmake ..
$ cmake --build .
```

# Usage

```
Usage: kss2vgm [Options] kssfile 
Options: 
  -p<play_time>  Specify song length in second to convert.
  -s<song_num>   Internal song number to play.
  -o<file>       Specify the output filename.
```

