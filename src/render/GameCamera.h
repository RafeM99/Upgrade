// GameCamera.h: follows the midpoint of the players and keeps both on screen.
// The world is always a 3D scene; the pixel-art stages use an exact orthographic side view
// at a fixed zoom, so every sprite pixel lands on one pixel of the stage's native buffer. At the last era the camera swings to a
// real perspective, slightly angled view: the 2.5D showpiece.
#pragma once
#include "raylib.h"

struct GameCamera {
    float x = 12;            // centre of view, tiles
    float viewH = 11;        // world tiles visible vertically
    float top = 3;           // world y (tiles, down) of the top of the view: the camera follows up and down
    bool pixelLock = false;  // pixel-art stage: fixed zoom, an exact orthographic view
    float swing = 0;         // 0 = flat side view, 1 = full 2.5D
    float shake = 0;
    float shakeX = 0, shakeY = 0;
    float aspect = 16.0f / 9.0f;
    // Where the players are (world tiles, y down), set by the Game every frame. The full 3D
    // stage uses it to zoom out until both players sit comfortably inside the frame.
    float focusMinX = 0, focusMaxX = 0, focusTop = 0, focusBottom = 0;
    bool hasFocus = false;

    float viewW() const { return viewH * aspect; }
    float left() const { return x - viewW() / 2; }
    float right() const { return x + viewW() / 2; }
    // targetTop: the view top the players want (the camera never shows below the floor or past minLeft)
    void update(float targetX, float targetTop, float spread, float minLeft, float dt);
    void setFocus(float minX, float maxX, float top, float bottom) { focusMinX = minX; focusMaxX = maxX; focusTop = top; focusBottom = bottom; hasFocus = true; }
    void addShake(float s) { if (s > shake) shake = s; }
    Camera3D camera3D(float& nearPlane, float& farPlane) const;
};
