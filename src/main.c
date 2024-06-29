#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>

#include <SDL2/SDL.h>

typedef enum AVPixelFormat PixFmt;
static PixFmt hw_pixel_format;

static const int sdl_pix_fmt = SDL_PIXELFORMAT_RGBA8888;
static const PixFmt ff_pix_fmt = AV_PIX_FMT_ABGR;

// Resize the frame and change the frame's pixel format
// TODO: make this faster
AVFrame* convert_frame(AVFrame* src, PixFmt fmt, int w, int h) {
    AVFrame* dst = av_frame_alloc();
    av_image_alloc(dst->data, dst->linesize, w, h, fmt, 1);
    dst->width = w;
    dst->height = h;
    dst->format = fmt;

    struct SwsContext* ctx = sws_getContext(src->width, src->height,
                                            src->format, w, h, fmt,
                                            SWS_BILINEAR, NULL, NULL, NULL);
    sws_scale(ctx, (const uint8_t* const*)src->data, src->linesize, 0,
              src->height, (uint8_t* const*)dst->data, dst->linesize);
    sws_freeContext(ctx);

    return dst;
}

typedef struct {
    int stream_index;
    enum AVMediaType type;
    const AVCodec* codec;

    AVCodecContext* ctx;
    AVBufferRef* hw_device_ctx;
} Decoder;

Decoder new_decoder(enum AVMediaType type) {
    Decoder decoder = {
        .stream_index = 0,
        .type = type,
        .ctx = NULL,
        .hw_device_ctx = NULL,
        .codec = NULL,
    };
    return decoder;
}

void free_decoder(Decoder* decoder) {
    av_buffer_unref(&decoder->hw_device_ctx);
    avcodec_free_context(&decoder->ctx);
}

// TODO: experiment and see if we can use hardware acceleration for audio
//       or is it just really just video only?
void find_hardware_device(Decoder* decoder) {
    enum AVHWDeviceType device_type = av_hwdevice_iterate_types(AV_HWDEVICE_TYPE_NONE);

    while (device_type != AV_HWDEVICE_TYPE_NONE) {
        const AVCodecHWConfig* config = avcodec_get_hw_config(decoder->codec, device_type);

        if (config != NULL &&
            config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) {
            hw_pixel_format = config->pix_fmt;

            int ret = av_hwdevice_ctx_create(&decoder->hw_device_ctx,
                                             device_type, NULL, NULL, 0);
            if (ret > 0) {
                // Found a hardware device
                break;
            }
        }
        device_type = av_hwdevice_iterate_types(device_type);
    }
}

PixFmt get_hw_pixel_format(AVCodecContext* ctx, const PixFmt* fmts) {
    const PixFmt* fmt;
    for (fmt = fmts; *fmt != AV_PIX_FMT_NONE; fmt++) {
        if (*fmt == hw_pixel_format) {
            return *fmt;
        }
    }
    av_log(NULL, AV_LOG_ERROR, "Couldn't get the hardware surface format\n");
    return AV_PIX_FMT_NONE;
}

int init_decoder(AVFormatContext* fmt_ctx, Decoder* decoder) {
    int ret;

    ret = av_find_best_stream(fmt_ctx, decoder->type, -1, -1, &decoder->codec, 0);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Couldn't find a media stream\n");
        return ret;
    }
    decoder->stream_index = ret;
    decoder->ctx = avcodec_alloc_context3(decoder->codec);

    AVStream* media = fmt_ctx->streams[decoder->stream_index];
    if (avcodec_parameters_to_context(decoder->ctx, media->codecpar) < 0) {
        return -1;
    }

    // Apparaently there's no hardware acceleration for audio
    // Only use hardware acceleration if we found a device supported by the codec
    find_hardware_device(decoder);
    if (decoder->hw_device_ctx) {
        decoder->ctx->hw_device_ctx = av_buffer_ref(decoder->hw_device_ctx);
        decoder->ctx->get_format = get_hw_pixel_format;
    }

    if ((ret = avcodec_open2(decoder->ctx, decoder->codec, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Couldn't open media decoder\n");
        return ret;
    }

    return 0;
}

typedef struct {
    SDL_Renderer* renderer;
    SDL_Texture* texture;
    void* texture_pixels;
    int width;
    int height;
    int fps;
} Renderer;

Renderer new_renderer(SDL_Window* window, int w, int h, int fps) {
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    SDL_Texture* texture = SDL_CreateTexture(renderer, sdl_pix_fmt,
                                             SDL_TEXTUREACCESS_STREAMING, w, h);
    Renderer r = {
        .renderer = renderer,
        .texture = texture,
        .texture_pixels = NULL,
        .width = w, .height = h,
        .fps = fps,
    };
    return r;
}

void free_renderer(Renderer* r) {
    SDL_DestroyTexture(r->texture);
    SDL_DestroyRenderer(r->renderer);
}

void resize_renderer(Renderer* r, int w, int h) {
    r->width = w;
    r->height = h;
    SDL_DestroyTexture(r->texture);
    r->texture = SDL_CreateTexture(r->renderer, sdl_pix_fmt,
                                   SDL_TEXTUREACCESS_STREAMING, w, h);
}

// Callback function to do something (rendering) with the frame data
typedef void (*FrameHandler)(void* data, uint8_t* pixels, int num_bytes);

void frame_handler(void* data, uint8_t* pixels, int num_bytes) {
    Renderer* r = (Renderer*)data;
    int pitch = r->width * 3;

    int speedup = 10;
    uint32_t fps_in_ms = (1000 / r->fps) - speedup;
    SDL_Delay(fps_in_ms);

    SDL_LockTexture(r->texture, NULL, &r->texture_pixels, &pitch);
    memcpy(r->texture_pixels, pixels, num_bytes);
    SDL_UnlockTexture(r->texture);

    SDL_Rect rect = {.x = 0, .y = 0, .w = r->width, .h = r->height};
    SDL_RenderClear(r->renderer);
    SDL_RenderCopy(r->renderer, r->texture, NULL, &rect);
    SDL_RenderPresent(r->renderer);
}

// This would also work for audio frames, not just video frames (they're both frames)
int decode_video_frame(AVCodecContext* ctx, AVPacket* packet,
                       void* handler_data, FrameHandler handler,
                       int w, int h) {
    AVFrame* hw_frame  = NULL;
    AVFrame* sw_frame  = NULL;
    AVFrame* frame     = NULL;
    AVFrame* rgb_frame = NULL;

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

        AVFrame* rgb_frame = convert_frame(frame, ff_pix_fmt, w, h);
        int size = av_image_get_buffer_size(rgb_frame->format, w, h, 1);
        buffer = malloc(size);
        ret = av_image_copy_to_buffer(buffer, size,
                                      (const uint8_t* const*)rgb_frame->data,
                                      (const int*)rgb_frame->linesize,
                                      rgb_frame->format, w, h, 1);
        handler(handler_data, buffer, size);

        error:
            av_frame_free(&hw_frame);
            av_frame_free(&sw_frame);
            av_frame_free(&rgb_frame);
            free(buffer);
            if (ret < 0) return ret;
    }

    return ret;
}

int main() {
    const char* file = "/home/aabiji/Videos/rat.webm";
    //const char* file = "/home/aabiji/Videos/10 rounds Bring sally up [TsWOIaYUBf4].webm";
    AVFormatContext* format_ctx = NULL;

    int ret = 0;
    if ((ret = avformat_open_input(&format_ctx, file, NULL, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Couldn't open input file\n");
        return ret;
    }

    if ((ret = avformat_find_stream_info(format_ctx, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Couldn't read stream info\n");
        return ret;
    }

    Decoder video_decoder = new_decoder(AVMEDIA_TYPE_VIDEO);
    if ((ret = init_decoder(format_ctx, &video_decoder)) < 0) {
        return ret;
    }
    int fps = av_q2d(format_ctx->streams[video_decoder.stream_index]->r_frame_rate);
    AVPacket* packet = av_packet_alloc();

    SDL_Init(SDL_INIT_EVERYTHING);
    SDL_Window* window = SDL_CreateWindow("Show Time!", SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED, 700, 500,
                                          SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    Renderer renderer = new_renderer(window, 700, 500, fps);
    SDL_Event event;
    bool closed = false;

    while (!closed) {
        ret = av_read_frame(format_ctx, packet);
        if (ret >= 0) {
            if (packet->stream_index == video_decoder.stream_index) {
                ret = decode_video_frame(video_decoder.ctx, packet,
                                        (void*)&renderer, frame_handler,
                                        renderer.width, renderer.height);
            }
        }
        av_packet_unref(packet);

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                closed = true;
                break;
            }

            if (event.type == SDL_WINDOWEVENT &&
                event.window.event == SDL_WINDOWEVENT_RESIZED) {
                resize_renderer(&renderer, event.window.data1, event.window.data2);
            }
        }
   }

    free_renderer(&renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    free_decoder(&video_decoder);
    av_packet_free(&packet);
    avformat_close_input(&format_ctx);
    return 0;
}