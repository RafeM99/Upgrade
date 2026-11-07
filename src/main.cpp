// Opens the window and runs the fixed 60 Hz game loop.
#include <cstdlib>
#include <string>
#include "raylib.h"
#include "rlgl.h"
#include "core/Config.h"
#include "core/Game.h"
#include "core/Paths.h"

int main(int argc, char** argv) {
    // Test options: --autoplay (players run right and jump), --fill (next era every 6 seconds),
    // --god (nobody dies), --frames N (quit after N frames), --shot-every N (save shot_<frame>.png).
    bool autoplay = false;
    bool fill = false;
    bool god = false;
    int maxFrames = 0;
    int shotEvery = 0;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--autoplay") autoplay = true;
        else if (arg == "--fill") fill = true;
        else if (arg == "--god") god = true;
        else if (arg == "--frames" && i + 1 < argc) maxFrames = std::atoi(argv[++i]);
        else if (arg == "--shot-every" && i + 1 < argc) shotEvery = std::atoi(argv[++i]);
    }

    Paths::init();
    Config::load(Paths::asset("config.ini"));

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT);
    InitWindow(Config::getInt("window", "width", 1280), Config::getInt("window", "height", 720), "Upgrade");
    SetTargetFPS(60);

    Game game;
    game.init();
    game.setAutoplay(autoplay);
    game.setAutoAdvance(fill);
    game.setGod(god);

    const float step = 1.0f / 60.0f;   // fixed timestep: physics is the same on every laptop
    float accumulator = 0.0f;
    int frame = 0;
    while (!WindowShouldClose()) {
        float frameTime = GetFrameTime();
        if (frameTime > 0.25f) frameTime = 0.25f;   // avoid a spiral after a long stall
        accumulator += frameTime;

        game.frameInput();
        while (accumulator >= step) {
            game.update(step);
            accumulator -= step;
        }

        ++frame;
        BeginDrawing();
        game.draw();
        if (shotEvery > 0 && frame % shotEvery == 0) {
            rlDrawRenderBatchActive();
            TakeScreenshot(TextFormat("shot_%05d.png", frame));
        }
        EndDrawing();
        if (maxFrames > 0 && frame >= maxFrames) break;
    }

    game.shutdown();
    CloseWindow();
    return 0;
}
