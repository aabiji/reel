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

struct Frame {
    // Presentation Time Stamp: timestamp of
    // when we should show the frame
    int pts;

    int size;
    uint8_t* data;
    AVFrame* ff_frame;

    void cleanup();
    Frame(AVFrame* _frame, int _pts);
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

    // Get a video or audio frame from the frame queue. The returned
    // frame's ff_frame field will be NULL if the queue is empty.
    Frame get_frame();

    // Resize a frame to the new width and height
    void resize_frame(Frame* frame, int new_width, int new_height);

    void queue_packet(AVPacket* packet);

    int get_sample_rate() { return codec_context->sample_rate; }
    int get_channel_count() { return codec_context->ch_layout.nb_channels; }

    // Presentation timestamp of the previously decoded frame,
    // or the predicted presentation timestamp of the next frame
    int clock;

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
    std::queue<Frame> frame_queue;

    const AVCodec* codec;
    AVCodecContext* codec_context;

    AVBufferRef* hw_device_ctx; // Hardware device context
    static PixelFormat hw_pixel_format; // Hardware pixel format

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
    bool stop;

private:
    std::thread video_thread;
    std::thread audio_thread;
    AVFormatContext* format_context;
};