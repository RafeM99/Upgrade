#include "render/GameCamera.h"
#include "level/Chunk.h"
#include "render/ArtSpec.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

void GameCamera::update(float targetX, float targetTop, float spread, float minLeft, float dt)
{
    // zoom out a little when the players spread apart (not in pixel-art stages: the zoom is fixed
    // there so the pixel grid never changes size)
    float wantH = art::VIEW_TILES_H;
    float needW = spread + 6.0f;
    if (!pixelLock && needW > wantH * aspect) wantH = std::min((float)LEVEL_H, needW / aspect);
    viewH = pixelLock ? art::VIEW_TILES_H : viewH + (wantH - viewH) * std::min(1.0f, 3.0f * dt);
    x += (targetX - x) * std::min(1.0f, 5.0f * dt);
    if (left() < minLeft) x = minLeft + viewW() / 2;
    float wantTop = std::max(0.0f, std::min((float)LEVEL_H - viewH, targetTop));
    top += (wantTop - top) * std::min(1.0f, 4.0f * dt);
    top = std::max(0.0f, std::min((float)LEVEL_H - viewH, top));
    shake = std::max(0.0f, shake - dt * 2.5f);
    shakeX = shake * ((std::rand() % 1000) / 500.0f - 1.0f) * 0.3f;
    shakeY = shake * ((std::rand() % 1000) / 500.0f - 1.0f) * 0.3f;
}

Camera3D GameCamera::camera3D(float& nearPlane, float& farPlane) const
{
    // swing 0..1: flat side view to the 2.5D angle. 1..2: on to the full 3D view, lower and
    // wider, looking down the street from in front of the path.
    float s1 = std::min(1.0f, std::max(0.0f, swing)), s2 = std::min(1.0f, std::max(0.0f, swing - 1.0f));
    float s = s1 * s1 * (3 - 2 * s1);
    float t = s2 * s2 * (3 - 2 * s2);
    float fov = 1.0f + (40.0f - 1.0f) * s + 6.0f * t;
    float halfH = viewH / 2;
    float dist = halfH / std::tan(fov * 0.5f * DEG2RAD) * (1.0f - 0.3f * t);
    // bottom of the view stays on the floor: centre y in 3D (y up, floor at 0)
    float cy = (float)LEVEL_H - (top + halfH);   // 3D y of the view centre (3D y points up)
    cy -= s * 1.2f + t * 1.6f;
    // 2.5D looks almost straight on (a slight turn shows the block sides) so the cut-out backdrop
    // and the path share one projection. 3D turns to a gentle 3/4 view and looks lower, so the
    // top third of the screen is sky.
    float yaw = -0.10f * s - 0.22f * t, pitch = 0.20f * s - 0.08f * t;
    Camera3D c{};
    if (swing <= 0.001f) {
        // flat stages: exact orthographic side view
        c.target = {x + shakeX, cy + shakeY, 0};
        c.position = {c.target.x, c.target.y, 50};
        c.up = {0, 1, 0};
        c.fovy = viewH;
        c.projection = CAMERA_ORTHOGRAPHIC;
        nearPlane = 1.0f;
        farPlane = 120.0f;
        return c;
    }
    c.target = {x + shakeX - s * 1.5f - t * 1.0f, cy + shakeY, -0.6f * t};
    c.position = {c.target.x + std::sin(yaw) * std::cos(pitch) * dist, c.target.y + std::sin(pitch) * dist,
                  c.target.z + std::cos(yaw) * std::cos(pitch) * dist};
    c.up = {0, 1, 0};
    c.fovy = fov;
    c.projection = CAMERA_PERSPECTIVE;
    nearPlane = std::max(0.05f, dist - 60.0f);
    farPlane = dist + 120.0f;
    return c;
}
