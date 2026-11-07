#include "raylib.h"
#include "rlgl.h"

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

    const char* title = "Upgrade";
    const int fontSize = 80;
    int frame = 0;

    while (!WindowShouldClose()) {
        ++frame;
        const bool lastFrame = maxFrames > 0 && frame >= maxFrames;

        BeginDrawing();
        ClearBackground(Color{20, 20, 30, 255});
        const int textWidth = MeasureText(title, fontSize);
        DrawText(title, (screenWidth - textWidth) / 2, (screenHeight - fontSize) / 2, fontSize, RAYWHITE);
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
