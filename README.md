# 🎬 Reel

A video player. This is my attempt at understanding how video
playback works.

Points to focus on:
- Rust FFI with ffmpeg C api
- Hardware accelerated decoding (audio and video)
- Audio and video synching (what if there's no audio/no video??)
    - Frame dropping if we lag too far behind
- Better architecture
- Being cognisent of memory usage/performance as to not crash the system
    - Fixed sized queue
    - Fast video and audio resampling
- How does h264 work?? Delve into FFMpeg to really understand how video is
  represented and decoded
- GUI controls
    - Play/Pause/Seeking
    - Audio volume
    - Playback speed
