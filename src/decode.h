#pragma once

#include <functional>
#include <mutex>
#include <queue>
#include <thread>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

using PixelFormat = enum AVPixelFormat;

// Thread safe queue
template <typename T>
class Queue {
public:
    void put(T value)
    {
        std::lock_guard<std::mutex> guard(mutex);
        queue.push(value);
    }

    T get()
    {
        std::lock_guard<std::mutex> guard(mutex);
        if (queue.empty()) {
            return T();
        }

        T front = queue.front();
        queue.pop();
        return front;
    }

private:
    std::queue<T> queue;
    std::mutex mutex;
};

class Frame {
public:
    Frame() { }

    // Video frame constructor, pass in
    // AVFrame so that it can be resized
    // on the user side
    Frame(AVFrame* frame, int _pts)
    {
        ff_frame = frame;
        pts = _pts;
    }

    // Audio frame constructor
    Frame(uint8_t* samples, int _size)
    {
        data = samples;
        size = _size;
    }

    void cleanup()
    {
        if (ff_frame != nullptr)
            av_frame_free(&ff_frame);
        if (data != nullptr)
            free(data);
    }

    // Presentation Time Stamp: timestamp of
    // when we should show the frame
    int pts = 0;

    int size = 0;
    uint8_t* data = nullptr;
    AVFrame* ff_frame = nullptr;
};

class MediaDecoder {
public:
    ~MediaDecoder();

    void init(AVFormatContext* context, bool is_video);

    // Runs on a separate thread, decoding audio or video
    // packets pulled from the packet queue and putting
    // the resulting frame into the frame queue.
    void process_frames();

    // Resize a frame to the new width and height
    void resize_frame(Frame* frame, int new_width, int new_height);

    int get_sample_rate() { return codec_context->sample_rate; }
    int get_channel_count() { return codec_context->ch_layout.nb_channels; }

    // Presentation timestamp of the previously decoded frame,
    // or the predicted presentation timestamp of the next frame
    int clock;

    // Number assigned to the particular audio or video stream
    int stream_index;

    // Video frame aspect ratio. Used to adapt the width to the
    // height. (width/height)
    double aspect_ratio;

    Queue<AVPacket*> packet_queue;
    Queue<Frame> frame_queue;

    bool stop;
    bool no_more_packets;
    bool initialized;

private:
    void decode_video_frame(AVPacket* packet);
    void decode_audio_samples(AVPacket* packet);

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

    // Unit of time used to measure frame timestamps
    // Converted from AVRational to decimal form
    double time_base;
};

class Decoder {
public:
    ~Decoder();

    void init(const char* file);

    // Queue incoming audio and video packets on a separate thread
    void decode_packets();

    int get_fps();
    void stop_threads();

    MediaDecoder video;
    MediaDecoder audio;
    bool initialized;
    bool stop;

private:
    std::thread video_thread;
    std::thread audio_thread;
    std::thread decode_thread;

    AVFormatContext* format_context;
};