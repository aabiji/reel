#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>

#include <SDL2/SDL.h>

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

// Resize the frame to (w, h)
// Convert the frame's pixel format to fmt
AVFrame* convert_frame(AVFrame* src, enum AVPixelFormat fmt, int w, int h) {
    AVFrame* dst = av_frame_alloc();
    av_image_alloc(dst->data, dst->linesize, w, h, fmt, 1);
    dst->width = w;
    dst->height = h;
    dst->format = fmt;

    struct SwsContext* converter = sws_getContext(src->width, src->height, src->format,
                                                  w, h, fmt, SWS_BILINEAR, NULL, NULL, NULL);
    sws_scale(converter, (const uint8_t* const*)src->data, src->linesize, 0,
              src->height, (uint8_t* const*)dst->data, dst->linesize);
    sws_freeContext(converter);

    return dst;
}

int decode(AVCodecContext* ctx, const AVPacket* packet, SDL_Renderer* renderer) {
    AVFrame* hw_frame = NULL;
    AVFrame* sw_frame = NULL;
    AVFrame* frame = NULL;
    AVFrame* rgba_frame = NULL;

    int buffer_size;
    uint8_t* buffer;

    int ret = 0;
    if ((ret = avcodec_send_packet(ctx, packet)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Couldn't decode packet\n");
        return ret;
    }

    int w = 500, h = 500;
    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24,
                                             SDL_TEXTUREACCESS_STREAMING, w, h);
    void* texture_pixels;
    SDL_Rect texture_rect = {.x = 0, .y = 0, .w = w, .h = h};
    int pixel_pitch = w * 3;

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

        AVFrame* rgba_frame = convert_frame(frame, AV_PIX_FMT_RGB24, w, h);
        int buffer_size = av_image_get_buffer_size(rgba_frame->format, w, h, 1);
        buffer = malloc(buffer_size);
        ret = av_image_copy_to_buffer(buffer, buffer_size,
                                      (const uint8_t* const*)rgba_frame->data, (const int*)rgba_frame->linesize,
                                      AV_PIX_FMT_RGB24, w, h, 1);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Couldn't copy frame");
            goto error;
        }
        SDL_LockTexture(texture, NULL, &texture_pixels, &pixel_pitch);
        memcpy(texture_pixels, buffer, buffer_size);
        SDL_UnlockTexture(texture);
        SDL_RenderCopy(renderer, texture, NULL, &texture_rect);
        SDL_RenderPresent(renderer);

        error:
            av_frame_free(&hw_frame);
            av_frame_free(&sw_frame);
            av_frame_free(&rgba_frame);
            free(buffer);
            if (ret < 0) {
                SDL_DestroyTexture(texture);
                return ret;
            }
    }

    SDL_DestroyTexture(texture);
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

    SDL_Init(SDL_INIT_EVERYTHING);
    const int size = 500;
    SDL_Window* window = SDL_CreateWindow("Show Time!", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                           size, size, SDL_WINDOW_SHOWN);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    SDL_Event event;
    bool closed = false;

    AVPacket* packet = av_packet_alloc();
    while (!closed) {
        SDL_RenderClear(renderer);

        if ((ret = av_read_frame(format_ctx, packet)) < 0)
            break;
        if (packet->stream_index == video_stream_index)
            ret = decode(decoder_ctx, packet, renderer);
        av_packet_unref(packet);

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT)
                closed = true;
        }
   }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    av_packet_free(&packet);
    av_buffer_unref(&hw_device_ctx);
    avcodec_free_context(&decoder_ctx);
    avformat_close_input(&format_ctx);
    return 0;
}