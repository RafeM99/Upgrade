#pragma once
// Follows the midpoint of the living players. Gameplay stays on the X/Y plane at z = 0.
// NES and SNES: flat orthographic side view. N64: perspective, a little above.
// GameCube: perspective from above and to the left. Wii: perspective that sways gently.
#include "raylib.h"

class Player;

class GameCamera {
public:
    Camera3D cam{};
    float viewHeight = 14.0f;   // world units visible top to bottom (at z = 0)

    void init();
    void update(float dt, const Player* players, int count, int era);
    float viewWidth() const;
    float focusX() const { return focus.x; }
    float focusY() const { return focus.y; }
    float leftEdge() const { return focus.x - viewWidth() * 0.5f; }
    float rightEdge() const { return focus.x + viewWidth() * 0.5f; }
    float bottomEdge() const { return focus.y - viewHeight * 0.5f; }
    float topEdge() const { return focus.y + viewHeight * 0.5f; }

private:
    Vector2 focus{0, 0};
    Vector3 offset{0, 0, 50};
    bool snapped = false;
    bool perspective = false;
    float time = 0.0f;
};
