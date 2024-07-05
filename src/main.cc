#include <SDL2/SDL.h>

#include "decode.h"

class Player
{
public:
    Player(SDL_Window* window, const char* file, int width, int height);
    void cleanup();

    static void video_handler(int size, uint8_t* samples);
    static void audio_handler(int size, uint8_t* samples);

    void resize(int new_width, int new_height);

    bool successful_init();

    Decoder decoder;
private:
    static SDL_Renderer* renderer;
    static SDL_Texture* frame_texture;
    static void* frame_pixels;
    static int frame_width;
    static int frame_height;

    SDL_AudioSpec wanted_spec;
    static SDL_AudioDeviceID device_id;
};

SDL_AudioDeviceID Player::device_id;
SDL_Renderer* Player::renderer;
SDL_Texture* Player::frame_texture;
void* Player::frame_pixels;
int Player::frame_width;
int Player::frame_height;

Player::Player(SDL_Window* window, const char* file, int width, int height)
{
    decoder.init(file, video_handler, audio_handler);
    if (decoder.initialized) {
        decoder.video.set_video_frame_size(width, height);
    }

    frame_width = width;
    frame_height = height;
    frame_pixels = NULL;
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    frame_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                                      SDL_TEXTUREACCESS_STREAMING, frame_width, frame_height);

    wanted_spec.freq = decoder.audio.get_sample_rate();
    wanted_spec.channels = decoder.audio.get_channel_count();
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.callback = NULL;
    wanted_spec.userdata = NULL;

    SDL_AudioSpec received;
    device_id = SDL_OpenAudioDevice(NULL, 0, &wanted_spec, &received, 0);
    SDL_PauseAudioDevice(device_id, 0);
}

void Player::cleanup()
{
    SDL_DestroyTexture(frame_texture);
    SDL_DestroyRenderer(renderer);
    SDL_CloseAudioDevice(device_id);
}

bool Player::successful_init()
{
    return decoder.initialized && decoder.audio.initialized && decoder.video.initialized;
}

void Player::video_handler(int size, uint8_t* pixels)
{
    int pitch = frame_width * 3;
    SDL_LockTexture(frame_texture, NULL, &frame_pixels, &pitch);
    memcpy(frame_pixels, pixels, size);
    SDL_UnlockTexture(frame_texture);

    SDL_Rect rect = {0, 0, frame_width, frame_height};
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, frame_texture, NULL, &rect);
    SDL_RenderPresent(renderer);
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

int main() {
    SDL_Init(SDL_INIT_EVERYTHING);
    SDL_Window* window = SDL_CreateWindow("Show Time!", SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED, 700, 500,
                                          SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    SDL_Event event;
    bool closed = false;

    const char* file = "/home/aabiji/Videos/fat.webm";
    Player player(window, file, 700, 500);
    if (!player.successful_init()) {
        return -1;
    }

    while (!closed) {
        player.decoder.decode_packet();

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                closed = true;
                break;
            }

            if (event.type == SDL_WINDOWEVENT &&
                event.window.event == SDL_WINDOWEVENT_RESIZED) {
                player.resize(event.window.data1, event.window.data2);
            }
        }
   }

    player.cleanup();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}