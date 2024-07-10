#include "player.h"

Player::Player(SDL_Window* window, const char* file, int width, int height)
{
    frame_width = width;
    frame_height = height;
    frame_pixels = nullptr;
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    frame_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_STREAMING, frame_width, frame_height);

    auto lambda = [this](int size, uint8_t* samples) {
        this->audio_handler(size, samples);
    };

    decoder.init(file, lambda);
    if (decoder.initialized) {
        decoder_thread = std::thread(&Decoder::decode_packets, &decoder);
    }

    wanted_spec.freq = decoder.audio.get_sample_rate();
    wanted_spec.channels = decoder.audio.get_channel_count();
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.callback = nullptr;
    wanted_spec.userdata = nullptr;

    SDL_AudioSpec received;
    device_id = SDL_OpenAudioDevice(nullptr, 0, &wanted_spec, &received, 0);
    SDL_PauseAudioDevice(device_id, 0);

    delay = 1000 / decoder.get_fps();
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

void Player::render_frame()
{
    Frame frame = decoder.video.get_frame();
    if (frame.ff_frame == nullptr)
        return;

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
}