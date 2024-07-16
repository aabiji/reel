# 🎬 Show Time!
I watch *a lot* of videos, so how does video playback work?
Bring in showtime, a vide player built in c++ using FFMpeg and SDL.
This project is more of a learning opportunity more than anything
so don't take this too seriously

To run:
```
sudo apt install ffmpeg libsdl2-dev libavdevice-dev

git clone https://github.com/aabiji/showtime

make run
```

## How it works

Multithreading:
We have a video frame decoding thread, audio frame decoding thread, packet decoding thread and our main app thread that has our event loop. In the packet decoding thread, we put packets into their respective queues. In the video frame decoding thread, we decode video frames and queue them so that we can display them. In the audio frame decoding thread, we decode audio samples and use SDL to queue them to our speaker.

Synchronizing video and audio:
So humans are more sensitive to changes in audio than we are to video, so we'll let the audio run normally, but we'll control the video playback. Instead of always having a fixed frame delay (fps), we can control the frame delay based on how far the video stream's timestamp is in reference to the audio stream's timestamp. If we're too far behind then we make the delay really small to try to catch up as fast as possible, if we're too far in front, we make the delay really big to slow down and let the audio catch up. Controlling the frame delay implies that we need to control how quickly SDL's event loop refreshes, and we can use SDL custom events for that.

Annoying opus bug:
The warnings plagged me for quite a while:
```
[opus @ 0x5ec0fc1b4580] Could not update timestamps for skipped samples.
[opus @ 0x5ec0fc1b4580] Could not update timestamps for discarded samples.
```
It seemed that whatever I tried, the warning would still generate. I even
resorted to adapting example code from ffmpeg themself, and yet the warning
remained. Exasperated I turned to stackoverflow and a wonderful person found
a solution. In a mpeg/aac/opus stream, ffmpeg inserts blank packets at the
start and end of the steam. These blank packets are skipped or discarded.
The decoder needs 2 parameters to update the timestamps for subsequent samples
and so we had the warning because we were missing one of them. The decoder needs
the audio sampling rate and the packet timebase, so setting the packet timebase
solved the issue!

## Resources:
- [FFmpeg examples](https://github.com/FFmpeg/FFmpeg/tree/master/doc/examples)
- [FFmpeg's own video player](https://github.com/FFmpeg/FFmpeg/blob/master/fftools/ffplay.c)
- [Basic video player tutorial](https://github.com/omgitsmoe/dranger-ffmpeg-updated)
- [Synchronizing audio and video](https://www.programmersought.com/article/21844834744/)

TODO:
- Ninja & mold for faster builds
- Pre initializing the scalers
- Synchronize audio and video
- Use a profiler to optimise the code
- See if there are hardware devices for decoding audio
- Renderer (OpenGL?)
- Basic UI (take a look at microui)
- Subtitles using WhisperCpp???
- Features
    - [] Play
    - [] Pause
    - [] Seek
    - [] Fullscreen
    - [] Volume control
    - [] Playback speed
