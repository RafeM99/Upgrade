#include "render/GameCamera.h"
#include "level/Chunk.h"
#include "render/ArtSpec.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

void GameCamera::update(float targetX, float spread, float minLeft, float dt)
{
    // zoom out a little when the players spread apart
    float wantH = art::VIEW_TILES_H;
    float needW = spread + 8.0f;
    if (needW > wantH * aspect) wantH = std::min(art::VIEW_TILES_H * 1.3f, needW / aspect);
    viewH += (wantH - viewH) * std::min(1.0f, 3.0f * dt);
    x += (targetX - x) * std::min(1.0f, 5.0f * dt);
    if (left() < minLeft) x = minLeft + viewW() / 2;
    shake = std::max(0.0f, shake - dt * 2.5f);
    shakeX = shake * ((std::rand() % 1000) / 500.0f - 1.0f) * 0.3f;
    shakeY = shake * ((std::rand() % 1000) / 500.0f - 1.0f) * 0.3f;
}

Camera3D GameCamera::camera3D(float& nearPlane, float& farPlane) const
{
    float s = swing * swing * (3 - 2 * swing);   // ease
    float m = modern * modern * (3 - 2 * modern);   // ease
    float fov = 1.0f + (40.0f - 1.0f) * s;
    fov += (30.0f - fov) * m * s;                    // modern: a longer lens, less distortion
    float vh = viewH * (1.0f - 0.06f * m);           // and a touch closer
    float halfH = vh / 2;
    float dist = halfH / std::tan(fov * 0.5f * DEG2RAD);
    // bottom of the view stays on the floor: centre y in 3D (y up, floor at 0)
    float cy = halfH + (LEVEL_H - viewH) * 0.0f;
    if (vh > LEVEL_H) cy = vh / 2;
    else cy = LEVEL_H - halfH + 0.0f;
    cy -= s * (1.2f - 0.5f * m);
    float yaw = -0.30f * s * (1.0f - 0.45f * m), pitch = 0.22f * s * (1.0f - 0.35f * m);
    Camera3D c{};
    c.target = {x + shakeX - s * (1.5f - 0.6f * m), cy + shakeY, 0};
    c.position = {c.target.x + std::sin(yaw) * std::cos(pitch) * dist, c.target.y + std::sin(pitch) * dist,
                  std::cos(yaw) * std::cos(pitch) * dist};
    c.up = {0, 1, 0};
    c.fovy = fov;
    c.projection = CAMERA_PERSPECTIVE;
    nearPlane = std::max(0.05f, dist - 60.0f);
    farPlane = dist + 120.0f;
    return c;
}
