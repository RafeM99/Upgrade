// GameCamera.h: follows the midpoint of the players and keeps both on screen.
// The world is always a 3D scene; eras 0-2 use a near-orthographic side view
// (a tiny field of view from far away). At the last era the camera swings to a
// real perspective, slightly angled view: the 2.5D showpiece.
#pragma once
#include "raylib.h"

struct GameCamera {
    float x = 12;            // centre of view, tiles
    float viewH = 14;        // world tiles visible vertically
    float swing = 0;         // 0 = flat side view, 1 = full 2.5D
    float shake = 0;
    float shakeX = 0, shakeY = 0;
    float aspect = 16.0f / 9.0f;

    float viewW() const { return viewH * aspect; }
    float left() const { return x - viewW() / 2; }
    float right() const { return x + viewW() / 2; }
    void update(float targetX, float spread, float minLeft, float dt);
    void addShake(float s) { if (s > shake) shake = s; }
    Camera3D camera3D(float& nearPlane, float& farPlane) const;
};
