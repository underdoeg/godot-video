#!/bin/bash

rsync -raP demo tweaklab@192.168.35.132:

ssh tweaklab@192.168.35.132 -t "cd demo && ANV_VIDEO_DECODE=1 DISPLAY=:0 ./godot.linuxbsd.editor.x86_64"