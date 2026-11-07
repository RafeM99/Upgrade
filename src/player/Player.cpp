#include "player/Player.h"
#include <cmath>
#include "core/Config.h"
#include "core/Events.h"
#include "level/Level.h"

namespace {
float approach(float v, float target, float step) {
    if (v < target) return std::fmin(v + step, target);
    return std::fmax(v - step, target);
}

// Of the columns the box covers, the one nearest its centre where test() holds; -1 if none.
template <typename Test>
int pickColumn(const Player& p, Test test) {
    const float eps = 0.0001f;
    int x0 = (int)std::floor(p.pos.x);
    int x1 = (int)std::floor(p.pos.x + Player::W - eps);
    int mid = (int)std::floor(p.centre().x);
    if (test(mid)) return mid;
    for (int x = x0; x <= x1; ++x)
        if (test(x)) return x;
    return -1;
}
}

void Player::spawn(Vector2 at) {
    pos = at;
    vel = {0, 0};
    lastSafe = at;
    dead = false;
    justDied = false;
    onGround = false;
    bumped = false;
    invuln = Config::getFloat("player", "respawn_invuln", 1.5f);
}

void Player::kill(bool force) {
    if (dead || god || (invuln > 0.0f && !force)) return;
    dead = true;
    justDied = true;
    deadTimer = Config::getFloat("player", "death_seconds", 1.2f);
    deathPos = pos;
    vel = {0.0f, 13.0f};   // the classic death hop
}

void Player::bounce() {
    vel.y = jumpHeld ? Config::getFloat("player", "stomp_bounce_high", 15.0f)
                     : Config::getFloat("player", "stomp_bounce", 10.0f);
    onGround = false;
}

void Player::update(float dt, const InputState& in, Level& level) {
    bumped = false;
    const float gravity = Config::getFloat("player", "gravity", 36.0f);

    if (dead) {
        deadTimer -= dt;
        vel.y -= gravity * dt;
        pos.y += vel.y * dt;
        return;
    }
    if (invuln > 0.0f) invuln -= dt;

    const float walkSpeed = Config::getFloat("player", "walk_speed", 6.0f);
    const float runSpeed = Config::getFloat("player", "run_speed", 9.5f);
    const float accel = Config::getFloat("player", "accel", 45.0f);
    const float jumpSpeed = Config::getFloat("player", "jump_speed", 15.0f);
    const float maxFall = Config::getFloat("player", "max_fall_speed", 22.0f);
    const float jumpCut = Config::getFloat("player", "jump_release_gravity", 2.0f);
    const float coyoteTime = Config::getFloat("player", "coyote_time", 0.1f);
    const float bufferTime = Config::getFloat("player", "jump_buffer", 0.1f);

    jumpHeld = in.jumpHeld;
    running = in.runHeld;
    if (in.moveX > 0.1f) facing = 1;
    if (in.moveX < -0.1f) facing = -1;
    float top = in.moveX * (running ? runSpeed : walkSpeed);
    vel.x = approach(vel.x, top, (onGround ? accel : accel * 0.6f) * dt);

    // Jump, with a little forgiveness at ledges (coyote) and for early presses (buffer).
    coyote = onGround ? coyoteTime : coyote - dt;
    jumpBuffer = in.jumpPressed ? bufferTime : jumpBuffer - dt;
    if (jumpBuffer > 0.0f && coyote > 0.0f) {
        // Running jumps go a little higher, like the old games.
        vel.y = jumpSpeed + std::fabs(vel.x) * 0.15f;
        jumpBuffer = 0.0f;
        coyote = 0.0f;
        events().playerJumped.emit(index);
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

    float prevTop = pos.y + H;
    onGround = false;
    pos.y += vel.y * dt;
    if (level.overlapsSolid(box(), index)) {
        if (vel.y < 0.0f) {
            pos.y = std::floor(pos.y) + 1.0f;
            onGround = true;
        } else if (vel.y > 0.0f) {
            int ty = (int)std::floor(pos.y + H);
            int tx = pickColumn(*this, [&](int x) { return level.solidFor(x, ty, index); });
            if (tx >= 0) {
                bumped = true;
                bumpX = tx;
                bumpY = ty;
            }
            pos.y = (float)ty - H - 0.001f;
        }
        vel.y = 0.0f;
    } else if (vel.y > 0.0f) {
        // Hidden blocks only exist for heads coming up from below. Surprise!
        int ty = (int)std::floor(pos.y + H - 0.0001f);
        if (prevTop <= (float)ty + 0.001f) {
            int tx = pickColumn(*this, [&](int x) { return level.tileAt(x, ty) == 'H'; });
            if (tx >= 0) {
                bumped = true;
                bumpX = tx;
                bumpY = ty;
                pos.y = (float)ty - H - 0.001f;
                vel.y = 0.0f;
            }
        }
    }

    if (onGround) {
        lastSafe = pos;
        int under = (int)std::floor(pos.y) - 1;
        const float eps = 0.0001f;
        for (int x = (int)std::floor(pos.x); x <= (int)std::floor(pos.x + W - eps); ++x)
            level.stepOn(x, under);   // fake floors notice you
    }

    if (pos.y < -3.0f) {
        if (god) spawn({lastSafe.x, lastSafe.y + 0.5f});
        else kill(true);   // pits ignore the respawn blink
    }
}
