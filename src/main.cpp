#include "raylib.h"
#include "rlgl.h"

#include "core/Game.h"

#include <cstdlib>
#include <cstring>

int main(int argc, char** argv)
{
    // Test options: --frames N quits after N frames, --shot FILE saves the last frame
    int maxFrames = 0;
    const char* shotFile = nullptr;
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--frames") == 0) maxFrames = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--shot") == 0) shotFile = argv[++i];
    }

    const int screenWidth = 1280;
    const int screenHeight = 720;
    InitWindow(screenWidth, screenHeight, "Upgrade");
    SetTargetFPS(60);

    Game game;
    game.init();

    // Fixed 60 Hz update with an accumulator; draw once per frame
    const float step = 1.0f / 60.0f;
    float accumulator = 0.0f;
    int frame = 0;

    while (!WindowShouldClose()) {
        ++frame;
        const bool lastFrame = maxFrames > 0 && frame >= maxFrames;

        float frameTime = GetFrameTime();
        if (frameTime > 0.25f) frameTime = 0.25f;
        accumulator += frameTime;
        while (accumulator >= step) {
            game.update(step);
            accumulator -= step;
        }

        BeginDrawing();
        game.draw();
        if (lastFrame && shotFile) {
            rlDrawRenderBatchActive();   // flush drawing before reading the screen
            Image shot = LoadImageFromScreen();
            ExportImage(shot, shotFile);     // saves to the exact path given
            UnloadImage(shot);
        }
        EndDrawing();

        if (lastFrame) break;
    }

    CloseWindow();
    return 0;
}
