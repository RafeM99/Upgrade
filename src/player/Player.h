#pragma once

// Owner: Rafale (core player). Starter from the skeleton: fill in movement here.

#include "raylib.h"

class Level;

class Player {
public:
    Vector2 pos{0.0f, 0.0f};   // bottom-centre of the player, in pixels
    Vector2 vel{0.0f, 0.0f};
    bool onGround = false;

    static constexpr float WIDTH = 28.0f;
    static constexpr float HEIGHT = 32.0f;

    void spawn(Vector2 at);
    void update(float dt, const Level& level);
    void draw() const;
};
