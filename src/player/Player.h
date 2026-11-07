// Player.h: one co-op player. Run, jump, stomp, the six abilities, Faded state.
// Position (x, y) is the top-left of the hitbox, in tiles.
#pragma once
#include "player/Abilities.h"
#include "player/Input.h"

class Level;

struct PlayerTuning {
    float runSpeed, accelGround, accelAir, gravity, jumpSpeed, jumpCut, maxFall, coyote, jumpBuffer;
    float doubleJumpSpeed, dashSpeed, dashTime, dashCooldown, poundSpeed, glideFall, wallSlide, wallJumpVx, wallJumpVy;
    float fadedSpeed, stompBounce, attackTime, attackCooldown, speedBoost;
    void load();
};

struct Player {
    int index = 0;
    bool joined = false;
    bool faded = false;
    float x = 0, y = 0, vx = 0, vy = 0;
    float w = 0.7f, h = 1.3f;
    int facing = 1;
    bool onGround = false;
    bool has[AB_COUNT] = {};

    // movement state
    float coyote = 0, jumpBuffer = 0, wallLock = 0;
    int airJumps = 0, wallDir = 0;
    float dashTime = 0, dashCooldown = 0;
    bool pounding = false, gliding = false, wallSliding = false;
    float attackTime = 0, attackCooldown = 0;
    float slowCooldown = 0;

    // co-op and power-ups
    float invuln = 0, reviveProgress = 0, offscreen = 0, fadedTime = 0;
    bool shield = false;
    float speedTime = 0, magnetTime = 0;

    // things that happened this tick, for the Game to react to (sounds, kills, shake)
    bool evJump = false, evDoubleJump = false, evWallJump = false, evDash = false, evPoundLand = false;
    bool evCrack = false, evAttack = false, evSlowRequest = false;

    float animTime = 0;
    int frame = 0;

    float cx() const { return x + w / 2; }
    float cy() const { return y + h / 2; }
    float bottom() const { return y + h; }
    bool alive() const { return joined && !faded; }

    void spawnAt(float px, float feetY);
    void update(const PlayerInput& in, Level& level, bool solo, bool attackUnlocked, float dt, const PlayerTuning& t);
};

// True if the box overlaps a tile that is solid for this player.
bool boxHitsSolid(const Level& level, float x, float y, float w, float h, int player, bool solo);
