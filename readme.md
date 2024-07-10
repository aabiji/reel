# 🎬 Show Time!
I watch *a lot* of videos, so how does video playback work?
Bring in showtime, a vide player built in c++ using FFMpeg and SDL.
This project is more of a learning opportunity more than anything
so don't take this too seriously

To run:
```
sudo apt install ffmpeg libsdl2-dev

git clone https://github.com/aabiji/showtime

make run
```

## How it works

Threads:
We have a video frame decoding thread, audio frame decoding thread, packet decoding thread and our main app thread that has our event loop. In the packet decoding thread, we put packets into their respective queues. In the video frame decoding thread, we decode video frames and queue them so that we can display them. In the audio frame decoding thread, we decode audio samples and use SDL to queue them to our speaker.

Synchronizing video and audio:
So humans are more sensitive to changes in audio than we are to video, so we'll let the audio run normally, but we'll control the video playback. Instead of always having a fixed frame delay (fps), we can control the frame delay based on how far the video stream's timestamp is in reference to the audio stream's timestamp. If we're too far behind then we make the delay really small to try to catch up as fast as possible, if we're too far in front, we make the delay really big to slow down and let the audio catch up. Controlling the frame delay implies that we need to control how quickly SDL's event loop refreshes, and we can use SDL custom events for that.

## Resources:
- [FFmpeg examples](https://github.com/FFmpeg/FFmpeg/tree/master/doc/examples)
- [FFmpeg's own video player](https://github.com/FFmpeg/FFmpeg/blob/master/fftools/ffplay.c)
- [Basic video player tutorial](https://github.com/omgitsmoe/dranger-ffmpeg-updated)
- [Synchronizing audio and video](https://www.programmersought.com/article/21844834744/)

TODO:
- Mutex to prevent data races when accessing the queues
- Synchronize audio and video
- Use a profiler to optimise the code
- See if there are hardware devices for decoding audio
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