#!/bin/bash

rsync -raP demo tweaklab@192.168.35.165:

ssh tweaklab@192.168.35.165 -t "cd demo && ANV_VIDEO_DECODE=1 DISPLAY=:0 ./godot.linuxbsd.editor.x86_64 --resolution 3840x2160"