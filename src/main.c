#include <stdio.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>

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

int main() {
    AVFormatContext* format_ctx = NULL;
    AVCodecContext* decoder_ctx;
    AVBufferRef* hw_device_ctx = NULL;

    const AVCodec* codec;
    int video_stream_index;
    int ret;

    av_log_set_level(AV_LOG_PANIC);

    const char* file = "/home/aabiji/Videos/rat.webm";
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

    av_buffer_unref(&hw_device_ctx);
    avcodec_free_context(&decoder_ctx);
    avformat_close_input(&format_ctx);
    return 0;
}