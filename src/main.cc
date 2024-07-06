#include <iostream>

#include "player.h"

int main() {
    SDL_Init(SDL_INIT_EVERYTHING);
    SDL_Window* window = SDL_CreateWindow("Show Time!", SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED, 700, 500,
                                          SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    SDL_Event event;
    bool closed = false;

    const char* file = "/home/aabiji/Videos/bring-sally-up.webm";
    Player player(window, file, 700, 500);
    if (!player.successful_init()) {
        return -1;
    }

    // TODO: make the threading less dodgy -- do we really need frame handlers????
    std::thread decoder_thread([&player]() {
        player.decoder.decode_packets();
    });

    while (!closed) {
        player.render_frame();

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                player.decoder.stop_threads(); // TODO: find a better way to stop the threads
                closed = true;
                break;
            }

            if (event.type == SDL_WINDOWEVENT &&
                event.window.event == SDL_WINDOWEVENT_RESIZED) {
                player.resize(event.window.data1, event.window.data2);
            }
        }

        SDL_Delay(player.delay);
   }

    player.cleanup();
    SDL_DestroyWindow(window);
    SDL_Quit();
    decoder_thread.join();
    return 0;
}