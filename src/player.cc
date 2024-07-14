#include "player.h"

Player::Player(SDL_Window* window, const char* file, int width, int height)
{
    decoder.init(file);
    if (decoder.initialized) {
        decoder_thread = std::thread(&Decoder::decode_packets, &decoder);
    }

    frame_width = width;
    frame_height = height;
    frame_pixels = nullptr;
    resized_to_aspect_ratio = false;

    window_ref = window;
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    frame_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_STREAMING, frame_width, frame_height);

    wanted_spec.freq = decoder.audio.get_sample_rate();
    wanted_spec.channels = decoder.audio.get_channel_count();
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.callback = nullptr;
    wanted_spec.userdata = nullptr;

    SDL_AudioSpec received;
    device_id = SDL_OpenAudioDevice(nullptr, 0, &wanted_spec, &received, 0);
    SDL_PauseAudioDevice(device_id, 0);

    last_frames_pts = 0;
    last_frames_delay = 40e-3;
}

void Player::cleanup()
{
    decoder.wait_for_threads();
    if (decoder_thread.joinable()) {
        decoder_thread.join();
    }

    SDL_DestroyTexture(frame_texture);
    SDL_DestroyRenderer(renderer);
    SDL_CloseAudioDevice(device_id);
}

bool Player::successful_init()
{
    return decoder.initialized && decoder.audio.initialized && decoder.video.initialized;
}

void Player::resize(int new_width, int new_height)
{
    double r = decoder.video.aspect_ratio;

    frame_height = new_height;
    frame_width = r != -1 ? int(r * double(new_height)) : new_width;

    // Do an initial window resize to match the video's aspect ratio
    if (!resized_to_aspect_ratio) {
        SDL_SetWindowSize(window_ref, frame_width, frame_height);
    }

    SDL_DestroyTexture(frame_texture);
    frame_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_STREAMING, frame_width, frame_height);
}

uint32_t timer_refresh_callback(uint32_t duration, void* opaque)
{
    SDL_Event custom_event;
    custom_event.type = REFRESH_EVENT;
    custom_event.user.code = 0;
    custom_event.user.data1 = nullptr;
    custom_event.user.data2 = nullptr;

    SDL_PushEvent(&custom_event);
    return 0; // Tell SDL to not call this callback again
}

void Player::schedule_a_video_refresh(int delay_in_ms)
{
    SDL_AddTimer(delay_in_ms, timer_refresh_callback, NULL);
}

void Player::refresh()
{
    Frame frame = decoder.video.frame_queue.get();
    if (frame.ff_frame == nullptr) {
        // Wait for more video frames
        schedule_a_video_refresh(1);
        return;
    }

    int delay = determine_delay(frame.pts);
    schedule_a_video_refresh(delay);
    render_frame(frame);
}

// TODO: video delay is way too fast
int Player::determine_delay(double pts)
{
    // Delay is the difference between the current pts and
    // the last frame's pts
    double delay = pts - last_frames_pts;
    bool invalid_delay = delay <= 0 || delay >= 1;
    if (invalid_delay) {
        delay = last_frames_delay;
    } else {
        last_frames_delay = delay;
    }
    last_frames_pts = pts;

    double reference_clock = decoder.audio.clock;

    // Acceptable range the frame's pts can be in
    double sync_threshold = 0.01;

    // We'll consider the frame out of range if it's
    // pts is bigger than this
    double out_of_sync_threshold = 10;

    double threshold = delay > sync_threshold ? delay : sync_threshold;
    double difference = pts - reference_clock;

    if (std::fabs(difference) < out_of_sync_threshold) {
        // The frame's pts is way behind the audio clock,
        // so make the delay really small to let the video
        // catch up to the audio
        if (difference <= -threshold) {
            delay = 0;
        }

        // The frame's pts is way ahead of the audio clock,
        // so make the delay really big to let the audio catch up
        else if (difference >= threshold) {
            delay *= 2;
        }
    }

    delay = delay < 0.01 ? 0.01 : delay;
    int delay_in_ms = int(delay) * 1000 + 0.5;
    return delay_in_ms;
}

void Player::render_frame(Frame& frame)
{
    // Do an initial window resize to match the video's aspect ratio
    if (!resized_to_aspect_ratio) {
        resize(frame_width, frame_height);
        resized_to_aspect_ratio = true;
    }

    decoder.video.resize_frame(&frame, frame_width, frame_height);

    int pitch = frame_width * 3;
    SDL_LockTexture(frame_texture, nullptr, &frame_pixels, &pitch);
    memcpy(frame_pixels, frame.data, frame.size);
    SDL_UnlockTexture(frame_texture);

    SDL_Rect rect = { 0, 0, frame_width, frame_height };
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, frame_texture, nullptr, &rect);
    SDL_RenderPresent(renderer);

    frame.cleanup();
}