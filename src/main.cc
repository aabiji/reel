#include "player.h"

int main()
{
    SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_EVENTS);
    SDL_Window* window = SDL_CreateWindow("Show Time!", SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED, 700, 500,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    SDL_Event event;
    bool closed = false;

    const char* file = "/home/aabiji/Videos/fat.webm";
    Player player(window, file, 700, 500);
    if (!player.successful_init()) {
        player.cleanup();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }

    player.schedule_a_video_refresh(50);
    while (!closed) {
        SDL_WaitEvent(&event);

        switch (event.type) {
        case SDL_QUIT:
            player.decoder.stop_threads(); // TODO: find a better way to stop the threads
            closed = true;
            break;
        case SDL_WINDOWEVENT:
            if (event.window.event == SDL_WINDOWEVENT_RESIZED)
                player.resize(event.window.data1, event.window.data2);
            break;
        case REFRESH_EVENT:
            player.refresh();
            break;
        }
    }

    player.cleanup();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}