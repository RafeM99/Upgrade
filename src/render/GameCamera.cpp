#include "render/GameCamera.h"
#include <algorithm>
#include <cmath>
#include "core/Config.h"
#include "player/Player.h"

void GameCamera::init() {
    viewHeight = Config::getFloat("camera", "view_height", 14.0f);
    focus = {viewWidth() * 0.5f, viewHeight * 0.5f - 1.5f};
    offset = {0.0f, 0.0f, 50.0f};
    perspective = false;
    snapped = false;
    cam.up = {0.0f, 1.0f, 0.0f};
    cam.fovy = viewHeight;   // in orthographic mode raylib uses fovy as the visible height
    cam.projection = CAMERA_ORTHOGRAPHIC;
    cam.target = {focus.x, focus.y, 0.0f};
    cam.position = {focus.x, focus.y, 50.0f};
}

float GameCamera::viewWidth() const {
    float h = (float)std::max(1, GetScreenHeight());
    return viewHeight * (float)GetScreenWidth() / h;
}

void GameCamera::update(float dt, const Player* players, int count, int era) {
    time += dt;
    float sumX = 0.0f, sumY = 0.0f;
    int n = 0;
    for (int i = 0; i < count; ++i) {
        if (!players[i].active || players[i].dead) continue;
        Vector2 c = players[i].centre();
        sumX += c.x;
        sumY += c.y;
        ++n;
    }
    if (n > 0) {
        float lookAhead = Config::getFloat("camera", "look_ahead", 3.0f);
        float targetX = sumX / n + lookAhead;
        float targetY = std::max(viewHeight * 0.5f - 1.5f, sumY / n + 1.0f);
        if (!snapped) {
            focus = {targetX, targetY};
            snapped = true;
        }
        float follow = 1.0f - std::exp(-Config::getFloat("camera", "follow_speed", 6.0f) * dt);
        focus.x += (targetX - focus.x) * follow;
        focus.y += (targetY - focus.y) * follow;
    }

    const float fovy = 45.0f;
    const float d = viewHeight * 0.5f / std::tan(fovy * 0.5f * DEG2RAD);
    Vector3 want = {0.0f, 0.0f, 50.0f};
    switch (era) {
        case 2: want = {0.0f, 2.5f, d}; break;                                   // N64
        case 3: want = {-3.5f, 4.0f, d * 0.95f}; break;                          // GameCube
        case 4: want = {std::sin(time * 0.4f) * 1.5f, 2.2f + std::sin(time * 0.6f) * 0.4f, d * 0.97f}; break;   // Wii
        default: break;                                                          // NES, SNES
    }
    bool persp = era >= 2;
    if (persp != perspective) {
        offset = want;
        perspective = persp;
    } else {
        float k = 1.0f - std::exp(-2.0f * dt);
        offset.x += (want.x - offset.x) * k;
        offset.y += (want.y - offset.y) * k;
        offset.z += (want.z - offset.z) * k;
    }

    cam.projection = persp ? CAMERA_PERSPECTIVE : CAMERA_ORTHOGRAPHIC;
    cam.fovy = persp ? fovy : viewHeight;
    cam.target = {focus.x, focus.y, 0.0f};
    cam.position = {focus.x + offset.x, focus.y + offset.y, offset.z};
}
