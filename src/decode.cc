extern "C" {
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/hwcontext.h>
}

#include "decode.h"

PixelFormat MediaDecoder::hw_pixel_format;

Decoder::~Decoder()
{
    avformat_close_input(&format_context);
    av_packet_free(&packet);
}

void Decoder::init(const char* file,
                   FrameHandler video_handler, FrameHandler audio_handler)
{
    initialized = false;

    int ret = 0;
    format_context = avformat_alloc_context();
    if ((ret = avformat_open_input(&format_context, file, NULL, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Couldn't open input file\n");
        return;
    }

    if ((ret = avformat_find_stream_info(format_context, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Couldn't read stream info\n");
        return;
    }

    packet = av_packet_alloc();
    samples_handler = audio_handler;
    frame_handler = video_handler;

    video.init(format_context, true);
    audio.init(format_context, false);
    initialized = video.initialized && audio.initialized;
}

void Decoder::decode_packet()
{
    int ret = av_read_frame(format_context, packet);
    if (ret < 0) return;

    if (packet->stream_index == video.stream_index) {
        // TODO: spawn audio playback and video rendering threads
        // so that they can concurrently. the video playback is slowing down
        // the audio playback, making it choppy
        //video.decode_video_frame(packet, frame_handler);
    } else if (packet->stream_index == audio.stream_index) {
        audio.decode_audio_samples(packet, samples_handler);
    }

    av_packet_unref(packet);
}

int Decoder::get_fps()
{
    return av_q2d(format_context->streams[video.stream_index]->r_frame_rate);;
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
        av_log(NULL, AV_LOG_ERROR, "Couldn't find a media stream\n");
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
    find_hardware_device();
    if (hw_device_ctx != NULL) {
        hw_device_ctx = av_buffer_ref(hw_device_ctx);
        codec_context->get_format = get_hw_pixel_format;
    }

    if ((ret = avcodec_open2(codec_context, codec, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Couldn't open media decoder\n");
        return;
    }

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
    av_log(NULL, AV_LOG_ERROR, "Couldn't get the hardware surface format\n");
    return AV_PIX_FMT_NONE;
}

void MediaDecoder::find_hardware_device()
{
    enum AVHWDeviceType device_type = av_hwdevice_iterate_types(AV_HWDEVICE_TYPE_NONE);

    while (device_type != AV_HWDEVICE_TYPE_NONE) {
        const AVCodecHWConfig* config = avcodec_get_hw_config(codec, device_type);

        if (config != NULL &&
            config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) {
            hw_pixel_format = config->pix_fmt;

            int ret = av_hwdevice_ctx_create(&hw_device_ctx,
                                             device_type, NULL, NULL, 0);
            if (ret > 0) { // Found a hardware device
                break;
            }
        }
        device_type = av_hwdevice_iterate_types(device_type);
    }
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

    SwrContext* ctx = NULL;
    swr_alloc_set_opts2(&ctx, &layout, new_format,
                        input_context->sample_rate, &input_context->ch_layout,
                        input_context->sample_fmt, input_context->sample_rate,
                        0, NULL);
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

int MediaDecoder::decode_audio_samples(AVPacket* packet, FrameHandler handler)
{
    AVFrame* frame = NULL;
    int ret = 0;

    int size = 0;
    uint8_t** audio = NULL;

    if ((ret = avcodec_send_packet(codec_context, packet)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Couldn't decode packet\n");
        return ret;
    }

    while (true) {
        frame = av_frame_alloc();
        ret = avcodec_receive_frame(codec_context, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            // At the end of packet or we need to send new input
            av_frame_free(&frame);
            return 0;
        } else if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Couldn't receive frame\n");
            av_frame_free(&frame);
            return ret;
        }

        // Convert the audio samples to the signed 16 bit format
        size = resample_audio(codec_context, frame, AV_SAMPLE_FMT_S16, &audio);
        handler(size, audio[0]);

        free(audio[0]);
        free(audio);
        av_frame_free(&frame);
    }

    return ret;
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
                                            NULL, NULL, NULL);
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

int MediaDecoder::decode_video_frame(AVPacket* packet, FrameHandler handler)
{
    AVFrame* hw_frame  = NULL;
    AVFrame* sw_frame  = NULL;
    AVFrame* frame     = NULL;

    int size = 0;
    uint8_t* pixels;

    int ret = 0;
    if ((ret = avcodec_send_packet(codec_context, packet)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Couldn't decode packet\n");
        return ret;
    }

    while (true) {
        hw_frame = av_frame_alloc();
        sw_frame = av_frame_alloc();

        ret = avcodec_receive_frame(codec_context, hw_frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR(EOF)) {
            // At the end of packet or we need to send new input
            av_frame_free(&hw_frame);
            av_frame_free(&sw_frame);
            return 0;
        } else if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Couldn't receive frame\n");
            goto error;
        }

        // format could also be AVSampleFormat
        if (hw_frame->format == hw_pixel_format) { // GPU decoded frame
            if ((ret = av_hwframe_transfer_data(sw_frame, hw_frame, 0)) < 0) {
                av_log(NULL, AV_LOG_ERROR, "Couldn't send frame from the GPU to the CPU\n");
                goto error;
            }
            frame = sw_frame;
        } else { // CPU decoded frame
            frame = hw_frame;
        }

        size = scale_frame(frame, AV_PIX_FMT_ABGR, frame_width, frame_height, &pixels);
        handler(size, pixels);

        error:
            av_frame_free(&hw_frame);
            av_frame_free(&sw_frame);
            free(pixels);
            if (ret < 0) return ret;
    }

    return ret;
}