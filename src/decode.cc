extern "C" {
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/hwcontext.h>
}

#include "decode.h"

// This is fine so long there is only 1 video MediaDecoder
PixelFormat MediaDecoder::hw_pixel_format;

Decoder::~Decoder()
{
    avformat_close_input(&format_context);
}

void Decoder::init(const char* file, AudioHandler audio_handler)
{
    initialized = false;

    int ret = 0;
    format_context = avformat_alloc_context();
    if ((ret = avformat_open_input(&format_context, file, nullptr, nullptr)) < 0) {
        av_log(nullptr, AV_LOG_ERROR, "Couldn't open input file\n");
        return;
    }

    if ((ret = avformat_find_stream_info(format_context, nullptr)) < 0) {
        av_log(nullptr, AV_LOG_ERROR, "Couldn't read stream info\n");
        return;
    }

    video.init(format_context, true);
    video_thread = std::thread(&MediaDecoder::process_video_frames, &video);

    audio.init(format_context, false);
    audio_thread = std::thread(&MediaDecoder::process_audio_samples, &audio, audio_handler);

    initialized = video.initialized && audio.initialized;
}

void Decoder::decode_packets()
{
    int ret = 0;
    while (ret == 0) {
        AVPacket* packet = av_packet_alloc();
        ret = av_read_frame(format_context, packet);

        if (packet->stream_index == video.stream_index) {
            video.queue_packet(packet);
        } else if (packet->stream_index == audio.stream_index) {
            audio.queue_packet(packet);
        } else {
            av_packet_free(&packet);
        }
    }
}

int Decoder::get_fps()
{
    return av_q2d(format_context->streams[video.stream_index]->r_frame_rate);;
}

void Decoder::stop_threads()
{
    video.stop = true;
    audio.stop = true;
}

void Decoder::wait_for_threads()
{
    video_thread.join();
    audio_thread.join();
}

MediaDecoder::~MediaDecoder()
{
    if (!initialized) return;
    av_buffer_unref(&hw_device_ctx);
    avcodec_free_context(&codec_context);
}

void MediaDecoder::init(AVFormatContext* context, bool is_video)
{
    initialized = false;
    int ret = 0;
    enum AVMediaType type = is_video ? AVMEDIA_TYPE_VIDEO : AVMEDIA_TYPE_AUDIO;

    ret = av_find_best_stream(context, type, -1, -1, &codec, 0);
    if (ret < 0) {
        av_log(nullptr, AV_LOG_ERROR, "Couldn't find a media stream\n");
        return;
    }

    stream_index = ret;
    codec_context = avcodec_alloc_context3(codec);

    AVStream* media = context->streams[stream_index];
    if (avcodec_parameters_to_context(codec_context, media->codecpar) < 0) {
        return;
    }

    // Apparaently there's no hardware acceleration for audio
    // Only use hardware acceleration if we found a device supported by the codec
    hw_device_ctx = nullptr;
    find_hardware_device();
    if (hw_device_ctx != nullptr) {
        codec_context->get_format = get_hw_pixel_format;
        codec_context->hw_device_ctx = av_buffer_ref(hw_device_ctx);
    }

    if ((ret = avcodec_open2(codec_context, codec, nullptr)) < 0) {
        av_log(nullptr, AV_LOG_ERROR, "Couldn't open media decoder\n");
        return;
    }

    stop = false;
    initialized = true;
}

PixelFormat MediaDecoder::get_hw_pixel_format(AVCodecContext* context,
                                         const PixelFormat* formats)
{
    const PixelFormat* format;
    for (format = formats; *format != AV_PIX_FMT_NONE; format++) {
        if (*format == hw_pixel_format) {
            return *format;
        }
    }
    av_log(nullptr, AV_LOG_ERROR, "Couldn't get the hardware surface format\n");
    return AV_PIX_FMT_NONE;
}

void MediaDecoder::find_hardware_device()
{
    enum AVHWDeviceType device_type = av_hwdevice_iterate_types(AV_HWDEVICE_TYPE_NONE);

    while (device_type != AV_HWDEVICE_TYPE_NONE) {
        const AVCodecHWConfig* config = avcodec_get_hw_config(codec, device_type);

        if (config != nullptr &&
            config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) {
            hw_pixel_format = config->pix_fmt;

            int ret = av_hwdevice_ctx_create(&hw_device_ctx,
                                             device_type, nullptr, nullptr, 0);
            if (ret == 0) { // Found a hardware device
                break;
            }
        }
        device_type = av_hwdevice_iterate_types(device_type);
    }
}

void MediaDecoder::queue_packet(AVPacket* packet)
{
    packet_queue.push(packet);
}

VideoFrame MediaDecoder::get_frame()
{
    if (frame_queue.empty()) {
        return VideoFrame();
    }

    VideoFrame frame = frame_queue.front();
    frame_queue.pop();
    return frame;
}

// Convert the audio samples format to the new format
// Write the resampled audio samples into samples and
// return the size in bytes of the samples array
int resample_audio(AVCodecContext* input_context, AVFrame* frame,
                   AVSampleFormat new_format, uint8_t*** samples)
{
    int channels = input_context->ch_layout.nb_channels;
    uint64_t layout_mask = AV_CH_LAYOUT_MONO;
    if (channels == 2) {
        layout_mask = AV_CH_LAYOUT_STEREO;
    } else if (channels > 2) {
        layout_mask = AV_CH_LAYOUT_SURROUND;
    }

    AVChannelLayout layout;
    av_channel_layout_from_mask(&layout, layout_mask);

    SwrContext* ctx = nullptr;
    swr_alloc_set_opts2(&ctx, &layout, new_format,
                        input_context->sample_rate, &input_context->ch_layout,
                        input_context->sample_fmt, input_context->sample_rate,
                        0, nullptr);
    swr_init(ctx);

    int size = 0;
    av_samples_alloc_array_and_samples(samples, &size,
                                       channels, frame->nb_samples,
                                       new_format, 1);

    swr_convert(ctx, *samples, frame->nb_samples,
               (const uint8_t**)frame->extended_data,
               frame->nb_samples);

    swr_free(&ctx);
    return size;
}

void MediaDecoder::decode_audio_samples(AVPacket* packet, AudioHandler handler)
{
    AVFrame* frame = nullptr;
    int ret = 0;

    int size = 0;
    uint8_t** audio = nullptr;

    if ((ret = avcodec_send_packet(codec_context, packet)) < 0) {
        av_log(nullptr, AV_LOG_ERROR, "Couldn't decode packet\n");
        return;
    }

    while (true) {
        frame = av_frame_alloc();
        ret = avcodec_receive_frame(codec_context, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            // At the end of packet or we need to send new input
            av_frame_free(&frame);
            return;
        } else if (ret < 0) {
            av_log(nullptr, AV_LOG_ERROR, "Couldn't receive frame\n");
            av_frame_free(&frame);
            return;
        }

        // Convert the audio samples to the signed 16 bit format
        size = resample_audio(codec_context, frame, AV_SAMPLE_FMT_S16, &audio);
        handler(size, audio[0]);

        free(audio[0]);
        free(audio);
        av_frame_free(&frame);
    }

    return;
}

void MediaDecoder::process_audio_samples(AudioHandler handler)
{
    while (!stop) {
        if (packet_queue.empty()) continue;

        AVPacket* packet = packet_queue.front();
        packet_queue.pop();

        decode_audio_samples(packet, handler);
        av_packet_free(&packet);
    }
}

void MediaDecoder::set_video_frame_size(int width, int height)
{
    frame_width = width;
    frame_height = height;
}

// Convert the frame's pixel format to the new format
// Also, resize the frame to the new width and height
// Write the resampled pixels into pixels and
// return the size in bytes of the pixels array
int scale_frame(AVFrame* frame, enum AVPixelFormat new_format,
                int new_width, int new_height, uint8_t** pixels)
{
    AVFrame* destination = av_frame_alloc();
    av_image_alloc(destination->data, destination->linesize,
                   new_width, new_height, new_format, 1);
    destination->width = new_width;
    destination->height = new_height;
    destination->format = new_format;

    enum AVPixelFormat format = (enum AVPixelFormat)frame->format;
    struct SwsContext* ctx = sws_getContext(frame->width, frame->height,
                                            format, new_width, new_height,
                                            new_format, SWS_BILINEAR,
                                            nullptr, nullptr, nullptr);
    sws_scale(ctx, (const uint8_t* const*)frame->data,
              frame->linesize, 0, frame->height,
              (uint8_t* const*)destination->data, destination->linesize);
    sws_freeContext(ctx);

    int size = av_image_get_buffer_size(new_format, new_width, new_height, 1);
    *pixels = new uint8_t[size];

    av_image_copy_to_buffer(*pixels, size,
                           (const uint8_t* const*)destination->data,
                           (const int*)destination->linesize,
                           new_format, new_width, new_height, 1);

    return size;
}

void MediaDecoder::decode_video_frame(AVPacket* packet)
{
    AVFrame* hw_frame  = nullptr;
    AVFrame* sw_frame  = nullptr;
    AVFrame* frame     = nullptr;

    int ret = 0;
    if ((ret = avcodec_send_packet(codec_context, packet)) < 0) {
        av_log(nullptr, AV_LOG_ERROR, "Couldn't decode packet\n");
        return;
    }

    while (true) {
        hw_frame = av_frame_alloc();
        sw_frame = av_frame_alloc();

        ret = avcodec_receive_frame(codec_context, hw_frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR(EOF)) {
            // At the end of packet or we need to send new input
            av_frame_free(&hw_frame);
            av_frame_free(&sw_frame);
            return;
        } else if (ret < 0) {
            av_log(nullptr, AV_LOG_ERROR, "Couldn't receive frame\n");
            av_frame_free(&hw_frame);
            av_frame_free(&sw_frame);
            return;
        }

        // format could also be AVSampleFormat
        if (hw_frame->format == hw_pixel_format) { // GPU decoded frame
            if ((ret = av_hwframe_transfer_data(sw_frame, hw_frame, 0)) < 0) {
                av_log(nullptr, AV_LOG_ERROR, "Couldn't send frame from the GPU to the CPU\n");
                av_frame_free(&hw_frame);
                av_frame_free(&sw_frame);
                return;
            }
            frame = sw_frame;
        } else { // CPU decoded frame
            frame = hw_frame;
        }

        VideoFrame vf;
        vf.size = scale_frame(frame, AV_PIX_FMT_ABGR, frame_width, frame_height, &vf.pixels);
        vf.width = frame_width;
        vf.height = frame_height;
        frame_queue.push(vf);

        av_frame_free(&hw_frame);
        av_frame_free(&sw_frame);
    }
}

void MediaDecoder::process_video_frames()
{
    while (!stop) {
        if (packet_queue.empty()) continue;

        AVPacket* packet = packet_queue.front();
        packet_queue.pop();

        decode_video_frame(packet);
        av_packet_free(&packet);
    }
}