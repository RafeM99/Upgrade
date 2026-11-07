#pragma once
// Owner: Systems (Camilla's Claude).
// Enemies and memory fragments. Spawned from the S G B F letters in chunk files.
// Done: spawning, fragments drift to the nearest player and fill the bar.
// To do (systems, 18:30 milestone): enemy movement, stomp and attack hits, fragment drops.
#include <vector>
#include "raylib.h"

class Game;

struct Enemy {
    char type;        // 'S' Static Blob, 'G' Glitch Flyer, 'B' Fog Brute
    Vector2 pos;      // centre
    Vector2 home;     // spawn point, for patrols and sine waves
    int hp;
    bool alive;
};

struct Fragment {
    Vector2 pos;      // centre
    bool taken;
};

class EnemyManager {
public:
    std::vector<Enemy> enemies;
    std::vector<Fragment> fragments;

    void clear();
    void update(float dt, Game& game);
};
