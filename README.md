## Build

```
mkdir build
cd build
cmake ..
cmake --build . --target x264 x265 zlib xz bzip2 libva libxcb-shared libxcb-static ffmpeg
cmake --build . --target godot-video
```