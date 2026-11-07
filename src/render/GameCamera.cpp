#include "render/GameCamera.h"
#include "level/Chunk.h"
#include "render/ArtSpec.h"
#include "core/Settings.h"

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

    // Full 3D (swing 1..2): the view is turned and looks lower, so the flat-stage framing lets
    // the players slip off the edges. Here the camera frames the players themselves: wide
    // enough for both of them plus room around, tall enough for the highest jump, and the view
    // top is raised by as much as the 3D view looks down.
    float t3 = std::min(1.0f, std::max(0.0f, swing - 1.0f));
    t3 = t3 * t3 * (3 - 2 * t3);
    float lift = 0;
    if (!pixelLock && t3 > 0 && hasFocus) {
        float viewCentreX = x - 1.5f - 1.0f * t3;   // where camera3D aims
        float halfW = std::max(viewCentreX - focusMinX, focusMaxX - viewCentreX) + 3.5f;
        // the HUD cards cover about the top two tiles of the 3D view: leave room under them
        float fitH = std::max(2.0f * halfW / aspect, (focusBottom - focusTop) + 7.5f);
        wantH = std::min((float)LEVEL_H, std::max(wantH, wantH + (fitH - wantH) * t3));
        lift = (2.8f + 2.2f) * t3;   // 2.8: how far the 3D view looks down; 2.2: room under the HUD
    }
    // zoom out quickly when someone nears an edge, back in gently
    float rate = t3 <= 0 ? 3.0f : (wantH > viewH ? 4.0f : 1.2f);   // other stages: as before
    viewH = pixelLock ? art::VIEW_TILES_H : viewH + (wantH - viewH) * std::min(1.0f, rate * dt);
    x += (targetX - x) * std::min(1.0f, 5.0f * dt);
    if (left() < minLeft) x = minLeft + viewW() / 2;
    float wantTop = std::max(-lift, std::min((float)LEVEL_H - viewH, targetTop - lift));
    top += (wantTop - top) * std::min(1.0f, 4.0f * dt);
    top = std::max(-lift, std::min((float)LEVEL_H - viewH, top));
    shake = std::max(0.0f, shake - dt * 2.5f);
    shakeX = shake * ((std::rand() % 1000) / 500.0f - 1.0f) * 0.3f;
    shakeY = shake * ((std::rand() % 1000) / 500.0f - 1.0f) * 0.3f;
    shakeX *= settings().shakeMul();   // player setting: Off / 50% / 100%
    shakeY *= settings().shakeMul();
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
    // (the 3D view used to move 30% closer here; that cut the players off, so it keeps the
    // same framing as 2.5D and zooms through viewH instead)
    float dist = halfH / std::tan(fov * 0.5f * DEG2RAD);
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
