#pragma once
// "Upgrade" on this branch: a Mario-style rage platformer. Each world ends at a flagpole,
// and the first four flags upgrade the whole game one console generation: NES, SNES, N64,
// GameCube, Wii. After that the worlds never end, and each one is meaner. The level is out
// to get you.
#include <string>
#include <vector>
#include "audio/Audio.h"
#include "core/EraManager.h"
#include "enemies/Enemies.h"
#include "level/ChunkSpawner.h"
#include "level/Level.h"
#include "player/Input.h"
#include "player/Player.h"
#include "render/GameCamera.h"
#include "render/Renderer.h"
#include "ui/Hud.h"

enum class Screen { Title, Playing };

class Game {
public:
    void init();
    void restart();
    void frameInput();       // once per rendered frame, before update()
    void update(float dt);   // fixed 60 Hz step
    void draw();             // once per rendered frame
    void shutdown();
    void setAutoplay(bool on);
    void setAutoAdvance(bool on) { autoAdvance = on; }   // test mode: next world every few seconds
    void setGod(bool on);                                // test mode: nobody dies

    Level level;
    ChunkSpawner spawner;
    EnemyManager enemies;
    EraManager era;
    Input input;
    Player players[2];
    GameCamera camera;
    Renderer renderer;
    Hud hud;
    Audio audio;

    Screen screen = Screen::Title;
    float freezeTimer = 0.0f;    // above zero while the era upgrade plays
    std::string popupTitle;      // "WORLD CLEAR!"
    std::string popupLine;       // "WELCOME TO THE N64 ERA"
    std::string popupTroll;      // a line of encouragement, sort of

    int coins = 0;
    int score = 0;
    int deaths = 0;
    float playTime = 0.0f;
    std::string deathLine;
    float deathTimer = 0.0f;     // death message on screen while above zero
    std::string titleNag;
    float titleNagTimer = 0.0f;
    int world = 0;               // 0-based and endless. The era stops at Wii; worlds do not.
    float worldStartX = 2.0f;

    void addCoin(Vector2 at, bool fromBlock);
    void addScore(int points, Vector2 at);
    void reachFlag(float flagX, float heightOnPole);
    float nextFlagX() const;     // -1 while the flag is not placed yet
    float leadX() const;         // x of the player furthest right

private:
    void onEraChanged(int e);
    void updatePlaying(float dt, const InputState in[2]);
    void handleBump(Player& p);
    void respawn(Player& p);
    void clearWorld();
    void skipToFlag();           // debug and --fill: teleport next to this world's flagpole
    std::string randomLine(const std::string& prefix) const;

    bool autoplay = false;
    bool autoAdvance = false;
    bool god = false;
    long frames = 0;
    float offscreen[2] = {0.0f, 0.0f};
};
