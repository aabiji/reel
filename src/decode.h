#pragma once

#include <queue>
#include <thread>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

// audio too????
struct VideoFrame
{
    uint8_t* pixels;
    int size;
};

using PixelFormat = enum AVPixelFormat;
using FrameHandler = void (*)(int size, uint8_t* buffer);

class MediaDecoder
{
public:
    ~MediaDecoder();

    void init(AVFormatContext* context, bool is_video);

    // Runs on a separate thread, fetching and decoding
    // video packets from the packet queue
    void process_video_frames();

    // Runs on a separate thread, fetching and decoding
    // audio packets from the packet queue
    void process_audio_samples(FrameHandler handler);

    void queue_packet(AVPacket* packet);

    void set_video_frame_size(int width, int height);
    int get_sample_rate() { return codec_context->sample_rate; }
    int get_channel_count() { return codec_context->ch_layout.nb_channels; }

    // Queue of decoded video frames
    std::queue<VideoFrame> frame_queue;

    bool initialized;
    int stream_index;
    bool stop;
private:
    void decode_video_frame(AVPacket* packet);
    void decode_audio_samples(AVPacket* packet, FrameHandler handler);

    // Iterate through all the possible hardware devices and
    // initialize the hardware device context based off the
    // hardware device the codec supports
    void find_hardware_device();

    // FFMpeg hardware device callback
    static PixelFormat get_hw_pixel_format(AVCodecContext* context,
                                           const PixelFormat* formats);

    std::queue<AVPacket*> packet_queue;

    const AVCodec* codec;
    AVCodecContext* codec_context;

    AVBufferRef* hw_device_ctx; // Hardware device context
    static PixelFormat hw_pixel_format; // Hardware pixel format

    int frame_width;
    int frame_height;
};

class Decoder
{
public:
    ~Decoder();
    void init(const char* file, FrameHandler audio_handler);

    void decode_packets(); // Runs on a separae thread
    int get_fps();

    void stop_threads();
    void wait_for_threads();

    MediaDecoder video;
    MediaDecoder audio;
    bool initialized;
private:
    AVFormatContext* format_context;
    std::thread video_thread;
    std::thread audio_thread;
};