## Build

### build with system ffmpeg

dependency on ubuntu
```
sudo apt install libavcodec-dev libavdevice-dev libavfilter-dev libavformat-dev libavutil-dev ffmpeg \
libva-dev libxcb-dri3-dev libvdpau-dev libdrm-dev libx11-xcb-dev libvpx-dev libdav1d-dev libopus-dev liblzma-dev libmp3lame-dev libglx-dev libx265-dev libx264-dev \
libaom-dev libbz2-dev libnuma-dev libfdk-aac-dev libvorbis-dev libbz2-dev libglx-dev
```

### Build with integrated ffmpeg
```
mkdir build
cd build
# cmake -DUSE_SYSTEM_FFMPEG=OFF ..
cmake --build . --target libx264 libx265 zlib xz bzip2 libxcb-shared libxcb-static ffmpeg
cmake --build . --target godot-video
```

library is linked against vaapi 2.2, older ubuntu version need the intel ppa
```sudo add-apt-repository -y ppa:kobuk-team/intel-graphics```