## Build

```
mkdir build
cd build
cmake ..
cmake --build . --target x264 x265 zlib xz bzip2 libva libxcb ffmpeg
cmake --build . --target godot-video
```