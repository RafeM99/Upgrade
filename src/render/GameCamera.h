#pragma once
// Owner: Core player (area 2). Starter code from the skeleton: replace freely.
// Side-on orthographic Camera3D that follows the midpoint of the active players.
// Gameplay stays on the X/Y plane at z = 0. The 2.5D swing (area 3) changes this camera.
#include "raylib.h"

class Player;

class GameCamera {
public:
    Camera3D cam{};
    float viewHeight = 14.0f;   // world units visible top to bottom

    void init();
    void update(float dt, const Player* players, int count);
    float viewWidth() const;
    float leftEdge() const { return cam.target.x - viewWidth() * 0.5f; }
    float rightEdge() const { return cam.target.x + viewWidth() * 0.5f; }
    float bottomEdge() const { return cam.target.y - viewHeight * 0.5f; }
    float topEdge() const { return cam.target.y + viewHeight * 0.5f; }

private:
    bool snapped = false;
};
