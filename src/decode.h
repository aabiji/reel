#pragma once

#include <functional>
#include <queue>
#include <thread>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

using PixelFormat = enum AVPixelFormat;
using AudioHandler = std::function<void(int, uint8_t*)>;

struct VideoFrame {
    int size;
    uint8_t* pixels;
    int width;
    int height;

    VideoFrame()
    {
        size = 0;
        pixels = nullptr;
        width = 0;
        height = 0;
    }
};

class MediaDecoder {
public:
    ~MediaDecoder();

    void init(AVFormatContext* context, bool is_video);

    // Runs on a separate thread, fetching and decoding
    // video packets from the packet queue. The decoded
    // frames are queued in the frame queue so the player
    // can play back those frames at its own rate.
    void process_video_frames();

    // Runs on a separate thread, fetching and decoding
    // audio packets from the packet queue. The decoded
    // samples are then handled by the player.
    void process_audio_samples(AudioHandler handler);

    // Get a video frame from the frame queue. The returned
    // frame's pixels field will be null if the queue is empty.
    VideoFrame get_frame();

    void queue_packet(AVPacket* packet);

    void set_video_frame_size(int width, int height);
    int get_sample_rate() { return codec_context->sample_rate; }
    int get_channel_count() { return codec_context->ch_layout.nb_channels; }

    bool initialized;
    int stream_index;
    bool stop;

private:
    void decode_video_frame(AVPacket* packet);
    void decode_audio_samples(AVPacket* packet, AudioHandler handler);

    // Iterate through all the possible hardware devices and
    // initialize the hardware device context based off the
    // hardware device the codec supports
    void find_hardware_device();

    // FFMpeg hardware device callback
    static PixelFormat get_hw_pixel_format(AVCodecContext* context,
        const PixelFormat* formats);

    std::queue<AVPacket*> packet_queue;
    std::queue<VideoFrame> frame_queue;

    const AVCodec* codec;
    AVCodecContext* codec_context;

    AVBufferRef* hw_device_ctx; // Hardware device context
    static PixelFormat hw_pixel_format; // Hardware pixel format

    int frame_width;
    int frame_height;

    // Unit of time used to measure frame timestamps
    // Converted from AVRational to decimal form
    double time_base;
};

class Decoder {
public:
    ~Decoder();

    void init(const char* file, AudioHandler audio_handler);

    // Queue incoming audio and video packets on a separate thread
    void decode_packets();

    int get_fps();
    void stop_threads();
    void wait_for_threads();

    MediaDecoder video;
    MediaDecoder audio;
    bool initialized;

private:
    std::thread video_thread;
    std::thread audio_thread;
    AVFormatContext* format_context;
};