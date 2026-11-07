#include "player/Player.h"
#include "core/Config.h"
#include "level/Level.h"
#include "render/ArtSpec.h"

#include <algorithm>
#include <cmath>

void PlayerTuning::load()
{
    const Config& c = config();
    runSpeed = c.f("player.run_speed", 7.5f);
    accelGround = c.f("player.accel_ground", 70.0f);
    accelAir = c.f("player.accel_air", 45.0f);
    gravity = c.f("player.gravity", 52.0f);
    jumpSpeed = c.f("player.jump_speed", 19.0f);
    jumpCut = c.f("player.jump_cut_speed", 7.0f);
    maxFall = c.f("player.max_fall", 22.0f);
    coyote = c.f("player.coyote_time", 0.1f);
    jumpBuffer = c.f("player.jump_buffer", 0.12f);
    doubleJumpSpeed = c.f("abilities.double_jump_speed", 16.5f);
    dashSpeed = c.f("abilities.dash_speed", 21.0f);
    dashTime = c.f("abilities.dash_time", 0.2f);
    dashCooldown = c.f("abilities.dash_cooldown", 0.55f);
    poundSpeed = c.f("abilities.pound_speed", 28.0f);
    glideFall = c.f("abilities.glide_fall_speed", 2.6f);
    wallSlide = c.f("abilities.wall_slide_speed", 4.0f);
    wallJumpVx = c.f("abilities.wall_jump_push", 9.0f);
    wallJumpVy = c.f("abilities.wall_jump_speed", 17.5f);
    fadedSpeed = c.f("coop.faded_speed_mult", 0.5f);
    stompBounce = c.f("player.stomp_bounce", 13.0f);
    attackTime = c.f("player.attack_time", 0.18f);
    attackCooldown = c.f("player.attack_cooldown", 0.35f);
    speedBoost = c.f("powerups.speed_mult", 1.4f);
}

bool boxHitsSolid(const Level& level, float x, float y, float w, float h, int player, bool solo)
{
    int x0 = (int)std::floor(x), x1 = (int)std::floor(x + w - 0.001f);
    int y0 = (int)std::floor(y), y1 = (int)std::floor(y + h - 0.001f);
    for (int ty = y0; ty <= y1; ++ty)
        for (int tx = x0; tx <= x1; ++tx)
            if (level.solidFor(tx, ty, player, solo)) return true;
    return false;
}

void Player::spawnAt(float px, float feetY)
{
    x = px - w / 2;
    y = feetY - h;
    vx = vy = 0;
    dashTime = 0;
    pounding = gliding = wallSliding = false;
    onGround = false;
    airJumps = 0;
}

void Player::update(const PlayerInput& in, Level& level, bool solo, bool attackUnlocked, float dt, const PlayerTuning& t)
{
    evJump = evDoubleJump = evWallJump = evDash = evPoundLand = evCrack = evAttack = evSlowRequest = false;
    auto dec = [dt](float& v) { v = std::max(0.0f, v - dt); };
    dec(dashCooldown); dec(attackTime); dec(attackCooldown); dec(slowCooldown); dec(invuln); dec(wallLock);
    dec(speedTime); dec(magnetTime);
    if (faded) fadedTime += dt;
    animTime += dt;

    const bool canAct = !faded;
    int move = (in.right ? 1 : 0) - (in.left ? 1 : 0);

    // ---- abilities and attack
    if (canAct && in.abilityPressed) {
        bool air = !onGround;
        if (in.down) {
            if (air && has[AB_GROUND_POUND] && !pounding) { pounding = true; dashTime = 0; vy = 0; }
            else if (has[AB_TIME_SLOW] && slowCooldown <= 0) evSlowRequest = true;
        } else {
            if (has[AB_DASH] && dashCooldown <= 0 && dashTime <= 0) {
                dashTime = t.dashTime;
                dashCooldown = t.dashCooldown;
                if (move) facing = move;
                evDash = true;
                pounding = false;
            } else if (air && has[AB_GROUND_POUND] && !pounding) { pounding = true; vy = 0; }
            else if (!has[AB_DASH] && !has[AB_GROUND_POUND] && has[AB_TIME_SLOW] && slowCooldown <= 0) evSlowRequest = true;
        }
    }
    if (canAct && attackUnlocked && in.attackPressed && attackCooldown <= 0) {
        attackTime = t.attackTime;
        attackCooldown = t.attackCooldown;
        evAttack = true;
    }

    // ---- horizontal
    float speed = t.runSpeed * (faded ? t.fadedSpeed : 1.0f) * (speedTime > 0 ? t.speedBoost : 1.0f);
    if (dashTime > 0) {
        dashTime -= dt;
        vx = facing * t.dashSpeed;
        vy = 0;
    } else if (pounding) {
        vx = 0;
        vy = t.poundSpeed;
    } else {
        if (move && wallLock <= 0) facing = move;
        float target = wallLock > 0 ? vx : move * speed;
        float acc = onGround ? t.accelGround : t.accelAir;
        if (vx < target) vx = std::min(target, vx + acc * dt);
        else if (vx > target) vx = std::max(target, vx - acc * dt);
        vy += t.gravity * dt;
    }

    // ---- jump buffer, coyote time, wall jump, double jump
    jumpBuffer = in.jumpPressed ? t.jumpBuffer : std::max(0.0f, jumpBuffer - dt);
    if (jumpBuffer > 0 && dashTime <= 0 && !pounding) {
        if (onGround || coyote > 0) {
            vy = -t.jumpSpeed * (faded ? 0.8f : 1.0f);
            jumpBuffer = coyote = 0;
            onGround = false;
            evJump = true;
        } else if (canAct && has[AB_WALL_JUMP] && wallDir != 0) {
            vy = -t.wallJumpVy;
            vx = -wallDir * t.wallJumpVx;
            facing = -wallDir;
            wallLock = 0.16f;
            jumpBuffer = 0;
            airJumps = 0;
            evWallJump = true;
        } else if (canAct && has[AB_DOUBLE_JUMP] && airJumps < 1) {
            vy = -t.doubleJumpSpeed;
            ++airJumps;
            jumpBuffer = 0;
            evDoubleJump = true;
        }
    }
    if (!in.jumpHeld && vy < -t.jumpCut && !pounding && wallLock <= 0) vy = -t.jumpCut;   // short hop

    // glide and wall slide
    gliding = canAct && has[AB_GLIDE] && in.jumpHeld && vy > 0 && !onGround && dashTime <= 0 && !pounding;
    if (gliding) vy = std::min(vy, t.glideFall);
    wallSliding = canAct && has[AB_WALL_JUMP] && !onGround && wallDir != 0 && move == wallDir && vy > 0;
    if (wallSliding) vy = std::min(vy, t.wallSlide);
    vy = std::min(vy, pounding ? t.poundSpeed : t.maxFall);

    // ---- move and collide, x then y
    const int pid = index;
    x += vx * dt;
    if (boxHitsSolid(level, x, y, w, h, pid, solo)) {
        if (vx > 0) x = std::floor(x + w) - w - 0.001f;
        else if (vx < 0) x = std::floor(x) + 1.0f + 0.001f;
        if (boxHitsSolid(level, x, y, w, h, pid, solo)) x -= vx * dt;   // safety
        vx = 0;
        dashTime = std::min(dashTime, 0.0f);
    }
    bool wasGround = onGround;
    onGround = false;
    y += vy * dt;
    if (vy > 0) {
        // ground pound breaks cracked floors under the player
        if (pounding) {
            int ty = (int)std::floor(y + h);
            bool broke = false;
            for (int tx = (int)std::floor(x); tx <= (int)std::floor(x + w - 0.001f); ++tx)
                if (level.tile(tx, ty) == '=') {
                    // break the whole cracked run so the gap is wide enough
                    for (int k = tx; level.tile(k, ty) == '='; --k) level.setTile(k, ty, '.');
                    for (int k = tx + 1; level.tile(k, ty) == '='; ++k) level.setTile(k, ty, '.');
                    for (int yy = ty + 1; yy < LEVEL_H && level.tile(tx, yy) == '='; ++yy) level.setTile(tx, yy, '.');
                    broke = true;
                }
            if (broke) evCrack = true;
        }
        if (boxHitsSolid(level, x, y, w, h, pid, solo)) {
            y = std::floor(y + h) - h - 0.001f;
            if (boxHitsSolid(level, x, y, w, h, pid, solo)) y -= vy * dt;
            vy = 0;
            onGround = true;
            if (pounding) { pounding = false; evPoundLand = true; }
        }
    } else if (vy < 0) {
        if (boxHitsSolid(level, x, y, w, h, pid, solo)) {
            y = std::floor(y) + 1.0f + 0.001f;
            vy = 0;
        }
    }
    if (onGround) { coyote = t.coyote; airJumps = 0; }
    else if (wasGround && vy >= 0) coyote = t.coyote;
    else coyote = std::max(0.0f, coyote - dt);

    // touching a wall?
    wallDir = 0;
    if (!onGround) {
        if (boxHitsSolid(level, x + 0.06f, y + 0.1f, w, h - 0.3f, pid, solo)) wallDir = 1;
        else if (boxHitsSolid(level, x - 0.06f, y + 0.1f, w, h - 0.3f, pid, solo)) wallDir = -1;
    }

    // ---- animation frame
    using namespace art;
    if (attackTime > 0) frame = PF_ATTACK;
    else if (pounding) frame = PF_POUND;
    else if (dashTime > 0) frame = PF_DASH;
    else if (gliding) frame = PF_SPECIAL;
    else if (wallSliding) frame = PF_WALL;
    else if (!onGround) frame = vy < 0 ? PF_JUMP : PF_FALL;
    else if (std::fabs(vx) > 0.5f) frame = PF_RUN0 + ((int)(animTime * std::fabs(vx) * 1.4f) % 4);
    else frame = PF_IDLE0 + ((int)(animTime * 2) % 2);
}
