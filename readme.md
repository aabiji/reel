# 🎬 Show Time !
A video player. Built in C using FFmpeg and SDL.
I watch *a lot* of videos, so how does video playback work?
[This is a nice guide](http://dranger.com/ffmpeg/tutorial01.html)

Setup:
```
sudo apt install ffmpeg libsdl2-dev
```

TODO -> audio playback:
- see if there are hardware devices for decoding audio
- Read stackoverflow audio explainers
- read encode_audio.c and understand what's going on
- implement a decode_audio_sample function
- play audio using sdl2 audiostream
- ???
- Use debugger to trace the code execution path
- Sync audio to video
- implement some sort of frame scheduling. right now we're just rendering
  as many frames as possible, but we need a way to control that

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