#pragma once
// Owner: Systems (Camilla's Claude). Shared hotspot: keep edits small and say so in your status file.
// Holds every part of the game and runs them in order. Each area keeps its own logic in its
// own files. Game only calls init, update and draw on them.
#include <set>
#include <string>
#include "audio/Audio.h"
#include "core/EraManager.h"
#include "core/MemoryBar.h"
#include "enemies/Enemies.h"
#include "level/ChunkSpawner.h"
#include "level/ForgettingWall.h"
#include "level/Level.h"
#include "player/Input.h"
#include "player/Player.h"
#include "render/GameCamera.h"
#include "render/Renderer.h"
#include "ui/Hud.h"

class Game {
public:
    void init();
    void frameInput();       // once per rendered frame, before update()
    void update(float dt);   // fixed 60 Hz step
    void draw();             // once per rendered frame
    void shutdown();
    void setAutoplay(bool on);
    void setAutoFill(bool on) { autoFill = on; }   // test mode: bar fills on its own

    Level level;
    ChunkSpawner spawner;
    ForgettingWall wall;
    EnemyManager enemies;
    EraManager era;
    MemoryBar bar;
    Input input;
    Player players[2];
    GameCamera camera;
    Renderer renderer;
    Hud hud;
    Audio audio;

    float freezeTimer = 0.0f;   // above zero while an era transition plays
    bool complete = false;      // bar reached 100
    std::string popupTitle;     // e.g. "MEMORY RESTORED: The first bike ride"
    std::string popupLine;      // e.g. "P1 NEW ABILITY: Double jump"

    std::set<std::string> unlockedAbilities() const;   // both players combined

private:
    void onEraChanged(int newEra);
    bool autoplay = false;
    bool autoFill = false;
    long frames = 0;
};
