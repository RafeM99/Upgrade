// Owner: Core player (area 2). Starter code from the skeleton: replace freely.
#include "render/GameCamera.h"
#include <algorithm>
#include <cmath>
#include "core/Config.h"
#include "player/Player.h"

void GameCamera::init() {
    viewHeight = Config::getFloat("camera", "view_height", 14.0f);
    cam.up = {0.0f, 1.0f, 0.0f};
    cam.fovy = viewHeight;   // in orthographic mode raylib uses fovy as the visible height
    cam.projection = CAMERA_ORTHOGRAPHIC;
    cam.target = {viewWidth() * 0.5f, viewHeight * 0.5f - 1.5f, 0.0f};
    cam.position = {cam.target.x, cam.target.y, 50.0f};
    snapped = false;
}

float GameCamera::viewWidth() const {
    float h = (float)std::max(1, GetScreenHeight());
    return viewHeight * (float)GetScreenWidth() / h;
}

void GameCamera::update(float dt, const Player* players, int count) {
    float sumX = 0.0f, sumY = 0.0f;
    int n = 0;
    for (int i = 0; i < count; ++i) {
        if (!players[i].active) continue;
        Vector2 c = players[i].centre();
        sumX += c.x;
        sumY += c.y;
        ++n;
    }
    if (n == 0) return;

    float lookAhead = Config::getFloat("camera", "look_ahead", 3.0f);
    float targetX = sumX / n + lookAhead;
    float targetY = std::max(viewHeight * 0.5f - 1.5f, sumY / n + 1.0f);
    if (!snapped) {
        cam.target.x = targetX;
        cam.target.y = targetY;
        snapped = true;
    }
    float follow = 1.0f - std::exp(-Config::getFloat("camera", "follow_speed", 6.0f) * dt);
    cam.target.x += (targetX - cam.target.x) * follow;
    cam.target.y += (targetY - cam.target.y) * follow;
    cam.fovy = viewHeight;
    cam.position = {cam.target.x, cam.target.y, 50.0f};
}
