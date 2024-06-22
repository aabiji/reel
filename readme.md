# 🎬 Show Time !
A video player. Built in C using FFmpeg and SDL.
I watch *a lot* of videos, so how does video playback work?
[This is a nice guide](http://dranger.com/ffmpeg/tutorial01.html)

Setup:
```
sudo apt install ffmpeg libsdl2-dev libsdl2-mixer-dev
```

TODO:
- refactor
- play video
- Use debugger to trace the code execution path
- Sync audio to video

Sub projects:
- Renderer (OpenGL?)
- GPU accelerated video decoding (ffmpeg stuff)
- Sound playback
- Some way to sync sound and video
- Do multiple things on different threads
- Basic UI (take a look at microui)
- Subtitles using WhisperCpp???
- Features
    - [] Play
    - [] Pause
    - [] Seek
    - [] Fullscreen
    - [] Volume control
    - [] Playback speed