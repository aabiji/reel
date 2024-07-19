#include "player.h"
#include "utils.h"

Player::Player(SDL_Window* window, const char* file, int width, int height)
{
    decoder.init(file);
    if (!decoder.initialized)
        return;

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
    wanted_spec.callback = sdl_audio_callback;
    wanted_spec.userdata = this;
    wanted_spec.silence = 0;

    SDL_AudioSpec received;
    device_id = SDL_OpenAudioDevice(nullptr, 0, &wanted_spec, &received, 0);
    SDL_PauseAudioDevice(device_id, 0);

    last_frames_pts = 0;
    last_frames_delay = 40e-3;
    read_index = 0;
}

void Player::cleanup()
{
    if (!successful_init())
        return;

    decoder.stop_threads();
    SDL_CloseAudioDevice(device_id);
    SDL_DestroyTexture(frame_texture);
    SDL_DestroyRenderer(renderer);
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

    // Center the frame texture
    frame_position = (new_width - frame_width) / 2;

    // Do an initial window resize to match the video's aspect ratio
    if (!resized_to_aspect_ratio) {
        SDL_SetWindowSize(window_ref, frame_width, frame_height);
        SDL_ShowWindow(window_ref);
        frame_position = 0;
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
        // The queue's empty, so wait for more video frames
        schedule_a_video_refresh(1);
        return;
    }

    int delay = determine_delay(frame.pts);
    render_frame(frame);
    frame.cleanup();

    schedule_a_video_refresh(delay);
}

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

    // Acceptable range the frame's pts can be in. We use a
    // threshold since video and audio can never be perfectly in sync
    double sync_threshold = 0.01;

    // If the frame's pts is outside the range,
    // then we consider it out of sync
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
    int delay_in_ms = int(delay * 1000 + 0.5);
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

    SDL_Rect rect = { frame_position, 0, frame_width, frame_height };
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, frame_texture, nullptr, &rect);
    SDL_RenderPresent(renderer);
}

void sdl_audio_callback(void* opaque, uint8_t* stream, int remaining)
{
    Player* player = (Player*)opaque;

    while (remaining > 0) {
        // Get our current frame
        Frame frame = player->decoder.audio.frame_queue.peek();
        if (frame.data == nullptr) {
            memset(stream, 0, remaining);
            break;
        }

        // Fill the stream with data from our audio frame
        int size = frame.size - player->read_index;
        int clamped_size = size > remaining ? remaining : size;
        memcpy(stream, frame.data + player->read_index, clamped_size);
        stream += size;
        remaining -= size;

        // Keep reading from one frame until we've read all the data from it
        player->read_index += clamped_size;
        if (player->read_index >= frame.size) {
            player->read_index = 0;
            // Continue to the next audio frame
            player->decoder.audio.frame_queue.get();
            player->decoder.audio.tick_clock(frame.pts);
        }
    }
}
