# 🎬 Show Time!
I watch *a lot* of videos, so how does video playback work?
Bring in showtime, a vide player built in c++ using FFMpeg and SDL.
This project is more of a learning opportunity more than anything
so don't take this too seriously

Setup:
```
sudo apt install ffmpeg libsdl2-dev
```

TODO:
- Mutex to prevent data races when accessing the queues
- Synchronize audio and video
- Resize the window
- Use a profiler to optimise the code
- See if there are hardware devices for decoding audio

Sub projects:
- Renderer (OpenGL?)
- GPU accelerated video decoding (ffmpeg stuff) -- check
- Sound playback -- check
- Some way to sync sound and video
- Do multiple things on different threads -- check
- Basic UI (take a look at microui)
- Subtitles using WhisperCpp???
- Features
    - [] Play
    - [] Pause
    - [] Seek
    - [] Fullscreen
    - [] Volume control
    - [] Playback speed