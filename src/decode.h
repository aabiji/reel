#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

using PixelFormat = enum AVPixelFormat;
using FrameHandler = void (*)(int size, uint8_t* buffer);

class MediaDecoder
{
public:
    ~MediaDecoder();

    void init(AVFormatContext* context, bool is_video);

    // Return a value < 0 on error
    int decode_audio_samples(AVPacket* packet, FrameHandler handler);

    // Return a value < 0 on error
    int decode_video_frame(AVPacket* packet, FrameHandler handler);

    void set_video_frame_size(int width, int height);

    int get_sample_rate() { return codec_context->sample_rate; }
    int get_channel_count() { return codec_context->ch_layout.nb_channels; }

    bool initialized;
    int stream_index;
private:
    // Iterate through all the possible hardware devices and
    // initialize the hardware device context based off the
    // hardware device the codec supports
    void find_hardware_device();

    // FFMpeg hardware device callback
    static PixelFormat get_hw_pixel_format(AVCodecContext* context,
                                           const PixelFormat* formats);

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
    void init(const char* file,
              FrameHandler video_handler, FrameHandler audio_handler);

    void decode_packet();
    int get_fps();

    MediaDecoder video;
    MediaDecoder audio;
    bool initialized;
private:
    AVPacket* packet;
    AVFormatContext* format_context;
    FrameHandler samples_handler;
    FrameHandler frame_handler;
};