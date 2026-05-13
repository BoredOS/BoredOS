#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <stdio.h>
#include <math.h>

// Screen dimensions
const int SCREEN_WIDTH = 600;
const int SCREEN_HEIGHT = 600;

int main(int argc, char* args[]) {
    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
        return 1;
    }

    // Create Window and Renderer
    SDL_Window* window = SDL_CreateWindow("Vinyl Clicker - C Version", 
                            SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 
                            SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_SHOWN);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

    // Load Vinyl Texture
    SDL_Texture* vinylTexture = IMG_LoadTexture(renderer, "vinyl.png");
    if (!vinylTexture) {
        printf("Failed to load vinyl.png! SDL_Error: %s\n", IMG_GetError());
        return 1;
    }

    // Variables
    double angle = 0;
    int score = 0;
    float spin_speed = 1.0f;
    int running = 1;
    SDL_Event e;

    // Define the destination rectangle (400x400 centered)
    SDL_Rect destRect = { 100, 100, 400, 400 };
    SDL_Point center = { 200, 200 }; // Center point relative to the rect

    // Main Game Loop
    while (running) {
        // Event Handling
        while (SDL_PollEvent(&e) != 0) {
            if (e.type == SDL_QUIT) {
                running = 0;
            }
            else if (e.type == SDL_MOUSEBUTTONDOWN) {
                int mx, my;
                SDL_GetMouseState(&mx, &my);
                
                // Pythagorean distance check
                float dist = sqrt(pow(mx - 300, 2) + pow(my - 300, 2));
                if (dist < 200) {
                    score++;
                    spin_speed += 5.0f;
                    printf("Plays: %d\n", score); // Console output for score
                }
            }
        }

        // Logic, Friction and Rotation
        if (spin_speed > 1.0f) {
            spin_speed *= 0.95f;
        }
        angle += spin_speed;

        // Rendering
        SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255); // Dark background
        SDL_RenderClear(renderer);

        // Draw the vinyl with rotation
        SDL_RenderCopyEx(renderer, vinylTexture, NULL, &destRect, angle, &center, SDL_FLIP_NONE);

        SDL_RenderPresent(renderer);

        // Cap at ~60 FPS
        SDL_Delay(16); 
    }

    // Clean up
    SDL_DestroyTexture(vinylTexture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    IMG_Quit();
    SDL_Quit();

    return 0;
}