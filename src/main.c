#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>

static enum AVPixelFormat hw_pixel_format;
enum AVPixelFormat get_hw_pixel_format(AVCodecContext* ctx, const enum AVPixelFormat* formats) {
    const enum AVPixelFormat* format;
    for (format = formats; *format != AV_PIX_FMT_NONE; format++) {
        if (*format == hw_pixel_format) {
            return *format;
        }
    }
    av_log(NULL, AV_LOG_ERROR, "Couldn't get the hardware surface format\n");
    return AV_PIX_FMT_NONE;
}

int decode(AVCodecContext* ctx, const AVPacket* packet) {
    AVFrame* hw_frame = NULL;
    AVFrame* sw_frame = NULL;
    AVFrame* frame = NULL;

    int buffer_size;
    uint8_t* buffer;

    int ret = 0;
    if ((ret = avcodec_send_packet(ctx, packet)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Couldn't decode packet\n");
        return ret;
    }

    while (true) {
        hw_frame = av_frame_alloc();
        sw_frame = av_frame_alloc();

        ret = avcodec_receive_frame(ctx, hw_frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR(EOF)) {
            // At the end of packet or we need to send new input
            av_frame_free(&hw_frame);
            av_frame_free(&sw_frame);
            return 0;
        } else if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Couldn't receive frame\n");
            goto error;
        }

        if (hw_frame->format == hw_pixel_format) { // GPU decoded frame
            if ((ret = av_hwframe_transfer_data(sw_frame, hw_frame, 0)) < 0) {
                av_log(NULL, AV_LOG_ERROR, "Couldn't send frame from the GPU to the CPU\n");
                goto error;
            }
            frame = sw_frame;
        } else { // CPU decoded frame
            frame = hw_frame;
        }

        buffer_size = av_image_get_buffer_size(frame->format, frame->width, frame->height, 1);
        buffer = malloc(buffer_size);
        ret = av_image_copy_to_buffer(buffer, buffer_size,
                                     (const uint8_t* const*)frame->data, (const int*)frame->linesize,
                                     frame->format, frame->width, frame->height, 1);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Couldn't copy frame");
            goto error;
        }

        error:
            av_frame_free(&hw_frame);
            av_frame_free(&sw_frame);
            free(buffer);
            if (ret < 0) return ret;
    }

    return ret;
}

int main() {
    const char* file = "/home/aabiji/Videos/rat.webm";

    AVFormatContext* format_ctx = NULL;
    AVCodecContext* decoder_ctx;
    AVBufferRef* hw_device_ctx = NULL;

    const AVCodec* codec;
    int video_stream_index; // Number indicating that a packet is a video packet
    int ret = 0;

    if ((ret = avformat_open_input(&format_ctx, file, NULL, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Couldn't open input file\n");
        return ret;
    }

    if ((ret = avformat_find_stream_info(format_ctx, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Couldn't read stream info\n");
        return ret;
    }

    if ((ret = av_find_best_stream(format_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Couldn't find a video stream\n");
        return ret;
    }
    video_stream_index = ret;

    enum AVHWDeviceType device_type = av_hwdevice_iterate_types(AV_HWDEVICE_TYPE_NONE);
    while (device_type != AV_HWDEVICE_TYPE_NONE) {
        const AVCodecHWConfig* config = avcodec_get_hw_config(codec, device_type);
        if (config != NULL && config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) {
            hw_pixel_format = config->pix_fmt;

            ret = av_hwdevice_ctx_create(&hw_device_ctx, device_type, NULL, NULL, 0);
            if (ret > 0) {
                printf("Found device supported by codec: %s", av_hwdevice_get_type_name(device_type));
                break;
            }
        }
        device_type = av_hwdevice_iterate_types(device_type);
    }

    decoder_ctx = avcodec_alloc_context3(codec);
    if (!decoder_ctx) {
        return AVERROR(ENOMEM);
    }

    AVStream* video = format_ctx->streams[video_stream_index];
    if (avcodec_parameters_to_context(decoder_ctx, video->codecpar) < 0) {
        return -1;
    }

    // Only use hardware acceleration if we found a device supported by the codec
    if (hw_device_ctx) {
        decoder_ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx);
        decoder_ctx->get_format = get_hw_pixel_format;
    }

    if ((ret = avcodec_open2(decoder_ctx, codec, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Couldn't open video decoder\n");
        return ret;
    }

    AVPacket* packet = av_packet_alloc();
    while (ret >= 0) {
        if ((ret = av_read_frame(format_ctx, packet)) < 0)
            break;

        if (packet->stream_index == video_stream_index)
            ret = decode(decoder_ctx, packet);
        av_packet_unref(packet);
   }

    av_packet_free(&packet);
    av_buffer_unref(&hw_device_ctx);
    avcodec_free_context(&decoder_ctx);
    avformat_close_input(&format_ctx);
    return 0;
}