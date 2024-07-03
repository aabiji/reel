# 🎬 Show Time !
A video player. Built in C using FFmpeg and SDL.
I watch *a lot* of videos, so how does video playback work?
[This is a nice guide](http://dranger.com/ffmpeg/tutorial01.html)

Setup:
```
sudo apt install ffmpeg libsdl2-dev
```

TODO -> audio playback:
- see if there are hardware devices for decoding audio -- there are but I'll need to revisit that topic
- Fix the audio playback choppiness and reverb
- Refactor -- split into mutliple files, use c++, comment
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
