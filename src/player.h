#pragma once

#include <SDL2/SDL.h>
#include "decode.h"

class Player
{
public:
    Player(SDL_Window* window, const char* file, int width, int height);
    void cleanup();

    static void audio_handler(int size, uint8_t* samples);

    void render_frame();

    void resize(int new_width, int new_height);

    bool successful_init();

    Decoder decoder;
    float delay;
private:
    SDL_Renderer* renderer;
    SDL_Texture* frame_texture;
    void* frame_pixels;
    int frame_width;
    int frame_height;

    SDL_AudioSpec wanted_spec;
    static SDL_AudioDeviceID device_id;
};