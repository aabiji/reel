#include "player.h"

SDL_AudioDeviceID Player::device_id;

Player::Player(SDL_Window* window, const char* file, int width, int height)
{
    frame_width = width;
    frame_height = height;
    frame_pixels = NULL;
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    frame_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                                      SDL_TEXTUREACCESS_STREAMING, frame_width, frame_height);

    decoder.init(file, audio_handler);
    if (decoder.initialized) {
        decoder.video.set_video_frame_size(width, height);
    }

    wanted_spec.freq = decoder.audio.get_sample_rate();
    wanted_spec.channels = decoder.audio.get_channel_count();
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.callback = NULL;
    wanted_spec.userdata = NULL;

    SDL_AudioSpec received;
    device_id = SDL_OpenAudioDevice(NULL, 0, &wanted_spec, &received, 0);
    SDL_PauseAudioDevice(device_id, 0);

    delay = 1000 / decoder.get_fps();
}

void Player::cleanup()
{
    decoder.wait_for_threads();
    SDL_DestroyTexture(frame_texture);
    SDL_DestroyRenderer(renderer);
    SDL_CloseAudioDevice(device_id);
}

bool Player::successful_init()
{
    return decoder.initialized && decoder.audio.initialized && decoder.video.initialized;
}

void Player::render_frame()
{
    if (decoder.video.frame_queue.empty()) return;
    VideoFrame frame = decoder.video.frame_queue.front();
    decoder.video.frame_queue.pop();

    int pitch = frame_width * 3;
    SDL_LockTexture(frame_texture, NULL, &frame_pixels, &pitch);
    memcpy(frame_pixels, frame.pixels, frame.size);
    SDL_UnlockTexture(frame_texture);

    SDL_Rect rect = {0, 0, frame_width, frame_height};
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, frame_texture, NULL, &rect);
    SDL_RenderPresent(renderer);

    free(frame.pixels);
}

void Player::audio_handler(int size, uint8_t* samples)
{
    SDL_QueueAudio(device_id, samples, size);
}

void Player::resize(int new_width, int new_height)
{
    frame_width = new_width;
    frame_height = new_height;
    SDL_DestroyTexture(frame_texture);
    frame_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                                      SDL_TEXTUREACCESS_STREAMING, new_width, new_height);
    decoder.video.set_video_frame_size(new_width, new_height);
}