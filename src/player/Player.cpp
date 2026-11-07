// Owner: Core player (area 2). Starter code from the skeleton: replace freely.
#include "player/Player.h"
#include <cmath>
#include "core/Config.h"
#include "level/Level.h"

namespace {
float approach(float v, float target, float step) {
    if (v < target) return std::fmin(v + step, target);
    return std::fmax(v - step, target);
}
}

void Player::spawn(Vector2 at) {
    pos = at;
    vel = {0, 0};
    lastSafe = at;
    faded = false;
    onGround = false;
}

void Player::update(float dt, const InputState& in, const Level& level) {
    const float runSpeed = Config::getFloat("player", "run_speed", 7.0f);
    const float accel = Config::getFloat("player", "accel", 60.0f);
    const float jumpSpeed = Config::getFloat("player", "jump_speed", 15.0f);
    const float gravity = Config::getFloat("player", "gravity", 36.0f);
    const float maxFall = Config::getFloat("player", "max_fall_speed", 22.0f);
    const float jumpCut = Config::getFloat("player", "jump_release_gravity", 2.0f);
    const float coyoteTime = Config::getFloat("player", "coyote_time", 0.1f);
    const float bufferTime = Config::getFloat("player", "jump_buffer", 0.1f);

    if (in.moveX > 0.1f) facing = 1;
    if (in.moveX < -0.1f) facing = -1;
    vel.x = approach(vel.x, in.moveX * runSpeed, accel * dt);

    // Jump, with a little forgiveness at ledges (coyote) and for early presses (buffer).
    coyote = onGround ? coyoteTime : coyote - dt;
    jumpBuffer = in.jumpPressed ? bufferTime : jumpBuffer - dt;
    if (jumpBuffer > 0.0f && coyote > 0.0f) {
        vel.y = jumpSpeed;
        jumpBuffer = 0.0f;
        coyote = 0.0f;
    }
    vel.y -= gravity * dt;
    if (!in.jumpHeld && vel.y > 0.0f) vel.y -= gravity * jumpCut * dt;   // short hop on early release
    if (vel.y < -maxFall) vel.y = -maxFall;

    // Move one axis at a time and push out of solid tiles.
    pos.x += vel.x * dt;
    if (level.overlapsSolid(box(), index)) {
        if (vel.x > 0.0f) pos.x = std::floor(pos.x + W) - W - 0.001f;
        else if (vel.x < 0.0f) pos.x = std::floor(pos.x) + 1.0f;
        vel.x = 0.0f;
    }

    onGround = false;
    pos.y += vel.y * dt;
    if (level.overlapsSolid(box(), index)) {
        if (vel.y < 0.0f) {
            pos.y = std::floor(pos.y) + 1.0f;
            onGround = true;
        } else if (vel.y > 0.0f) {
            pos.y = std::floor(pos.y + H) - H - 0.001f;
        }
        vel.y = 0.0f;
    }

    if (onGround) lastSafe = pos;
    // Placeholder until Faded exists: falling into a pit puts you back on the last ledge.
    if (pos.y < -8.0f) spawn({lastSafe.x, lastSafe.y + 0.5f});
}
