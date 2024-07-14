#pragma once

#include "decode.h"
#include <SDL2/SDL.h>

const uint32_t REFRESH_EVENT = SDL_USEREVENT + 1;

class Player {
public:
    Player(SDL_Window* window, const char* file, int width, int height);

    void cleanup();

    // Called on a REFRESH_EVENT event. Renders the next frame.
    // Determines the frame delay based on how the video
    // clock compares to the audio clock.
    void refresh();

    void schedule_a_video_refresh(int delay_in_ms);

    // Resize the frame texture
    void resize(int new_width, int new_height);

    bool successful_init();

private:
    void render_frame(Frame& frame);

    // Determine a video delay that will synchronize
    // the audio and video together
    int determine_delay(double pts);

    int last_frames_pts;
    int last_frames_delay;
    Decoder decoder;

    SDL_Window* window_ref;
    SDL_Renderer* renderer;
    SDL_Texture* frame_texture;
    void* frame_pixels;
    int frame_width;
    int frame_height;
    bool resized_to_aspect_ratio;

    SDL_AudioSpec wanted_spec;
    SDL_AudioDeviceID device_id;
};