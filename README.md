## Build

```
mkdir build
cd build
cmake ..
cmake --build . --target libx264 libx265 zlib xz bzip2 libva libxcb-shared libxcb-static ffmpeg
cmake --build . --target godot-video
```