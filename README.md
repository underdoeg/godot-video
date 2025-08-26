## Build

### build with system ffmpeg

dependency on ubuntu
```
sudo apt install libavcodec-dev libavdevice-dev libavfilter-dev libavformat-dev libavutil-dev ffmpeg
```

```
mkdir build
cd build
cmake --build . --target godot-video
```

### Build with integrated ffmpeg
```
mkdir build
cd build
cmake -DUSE_SYSTEM_FFMPEG=OFF ..
cmake --build . --target libx264 libx265 zlib xz bzip2 libva libxcb-shared libxcb-static ffmpeg
cmake --build . --target godot-video
```