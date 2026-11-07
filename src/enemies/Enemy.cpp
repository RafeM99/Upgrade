#include "enemies/Enemy.h"
#include "core/Config.h"
#include "level/Level.h"
#include "player/Player.h"

#include <algorithm>
#include <cmath>

void EnemyTuning::load()
{
    const Config& c = config();
    blobSpeed = c.f("enemies.blob_speed", 1.7f);
    flyerSpeed = c.f("enemies.flyer_drift_speed", 1.3f);
    bruteSpeed = c.f("enemies.brute_speed", 1.2f);
    bruteCharge = c.f("enemies.brute_charge_speed", 6.5f);
    speedPerEra = c.f("enemies.speed_per_era", 0.15f);
    bruteHp = c.i("enemies.brute_hp", 3);
    drops[EN_BLOB] = c.i("enemies.blob_fragments", 1);
    drops[EN_FLYER] = c.i("enemies.flyer_fragments", 2);
    drops[EN_BRUTE] = c.i("enemies.brute_fragments", 5);
}

Enemy makeEnemy(int type, float tx, float ty, int era, const EnemyTuning& t)
{
    Enemy e;
    e.type = type;
    if (type == EN_BLOB) { e.w = 0.8f; e.h = 0.72f; }
    if (type == EN_FLYER) { e.w = 0.7f; e.h = 0.6f; }
    if (type == EN_BRUTE) { e.w = 1.5f; e.h = 1.8f; e.hp = t.bruteHp; }
    e.x = tx + 0.5f - e.w / 2;
    e.y = ty + 1.0f - e.h;
    e.baseY = e.y;
    e.t = (float)((int)(tx * 7) % 10);
    (void)era;
    return e;
}

namespace {
const Player* nearest(const Player* ps, float x, float y, float& dist)
{
    const Player* best = nullptr;
    dist = 1e9f;
    for (int i = 0; i < 2; ++i) {
        if (!ps[i].alive()) continue;
        float d = std::hypot(ps[i].cx() - x, ps[i].cy() - y);
        if (d < dist) { dist = d; best = &ps[i]; }
    }
    return best;
}

bool solidE(const Level& L, float x, float y, float w, float h) { return boxHitsSolid(L, x, y, w, h, -1, false); }
}  // namespace

void updateEnemy(Enemy& e, Level& level, const Player* players, float dt, int era, float timeScale, const EnemyTuning& tu)
{
    if (e.dead) {
        e.deadTime += dt;
        if (e.deadTime > 0.45f) e.remove = true;
        return;
    }
    dt *= timeScale;
    e.t += dt;
    e.hurt = std::max(0.0f, e.hurt - dt);
    float eraK = 1.0f + tu.speedPerEra * era;

    if (e.type == EN_FLYER) {
        float d;
        const Player* p = nearest(players, e.cx(), e.cy(), d);
        if (p && d < 12) {
            float dx = p->cx() - e.cx();
            e.dir = dx < 0 ? -1 : 1;
            e.x += e.dir * tu.flyerSpeed * eraK * dt;
            e.baseY += (std::min(p->y - 1.5f, 10.0f) - e.baseY) * 0.3f * dt;
        }
        e.y = e.baseY + std::sin(e.t * 2.4f) * 1.1f;
        if (e.y < 0.5f) e.y = 0.5f;
        // glitch: blink a short distance now and then (16-bit era on)
        if (era >= 2 && std::fmod(e.t, 3.0f) < dt) e.x += e.dir * 1.2f;
        return;
    }

    // walkers: blob and brute
    float speed = (e.type == EN_BLOB ? tu.blobSpeed : tu.bruteSpeed) * eraK;
    if (e.type == EN_BRUTE) {
        float d;
        const Player* p = nearest(players, e.cx(), e.cy(), d);
        e.stateTime += dt;
        if (e.state == 0 && era >= 2 && p && d < 7 && std::fabs(p->cy() - e.cy()) < 2 && e.onGround) {
            e.state = 1;
            e.stateTime = 0;
            e.dir = p->cx() < e.cx() ? -1 : 1;
        } else if (e.state == 1 && e.stateTime > 0.55f) { e.state = 2; e.stateTime = 0; }
        else if (e.state == 2 && e.stateTime > 0.9f) { e.state = 0; e.stateTime = 0; }
        if (e.state == 1) speed = 0;
        if (e.state == 2) speed = tu.bruteCharge;
    }
    if (e.hurt > 0) speed *= 0.2f;
    e.vx = e.dir * speed;
    e.vy = std::min(e.vy + 50 * dt, 20.0f);

    e.x += e.vx * dt;
    if (solidE(level, e.x, e.y, e.w, e.h)) {
        e.x -= e.vx * dt;
        e.dir = -e.dir;
        if (e.state == 2) { e.state = 0; e.stateTime = 0; }
    }
    e.y += e.vy * dt;
    e.onGround = false;
    if (solidE(level, e.x, e.y, e.w, e.h)) {
        if (e.vy > 0) { e.y = std::floor(e.y + e.h) - e.h - 0.001f; e.onGround = true; }
        else e.y = std::floor(e.y) + 1.001f;
        e.vy = 0;
    }
    // turn at ledges
    if (e.onGround) {
        float ahead = e.dir > 0 ? e.x + e.w + 0.05f : e.x - 0.05f;
        if (!level.solidFor((int)std::floor(ahead), (int)std::floor(e.y + e.h + 0.1f), -1, false)) {
            e.dir = -e.dir;
            if (e.state == 2) { e.state = 0; e.stateTime = 0; }
        }
    }
    if (e.y > LEVEL_H + 2) e.remove = true;
}

// ------------------------------------------------------------------ Boss
void Boss::start(float camX, int era, int hpBase, int hpPerEra)
{
    active = true;
    defeated = false;
    maxHp = hp = hpBase + era * hpPerEra;
    state = 0;
    stateTime = 0;
    t = 0;
    hurt = 0;
    spawnTimer = 3;
    attacks = 0;
    charging = false;
    landed = false;
    vy = 0;
    kind = era <= 1 ? BK_ALARM : BK_HOURGLASS;
    x = camX + 16;
    y = -3;
    if (kind == BK_ALARM) { x = camX + 5; y = -4; }
    fromX = x; fromY = y;
    toX = camX + 6;
    toY = 4;
}

void Boss::update(float dt, float camLeft, float camRight, const Level& level, const Player* players, int era, float timeScale)
{
    if (!active) return;
    dt *= timeScale;
    t += dt;
    stateTime += dt;
    hurt = std::max(0.0f, hurt - dt);
    wantsSpawn = false;
    landed = false;
    auto ease = [](float k) { k = std::min(1.0f, std::max(0.0f, k)); return k * k * (3 - 2 * k); };
    float hoverX = camLeft + (camRight - camLeft) * 0.7f;
    // The floor under gx: the first solid tile at or below row `from` with open air above it,
    // so bosses land on the ground and not on a ledge high up the screen.
    auto groundY = [&](float gx, float from = 6.0f) {
        int tx = (int)std::floor(gx);
        for (int ty = std::max(1, (int)from); ty < LEVEL_H; ++ty)
            if (level.isSolidChar(level.tile(tx, ty)) && !level.isSolidChar(level.tile(tx, ty - 1))) return ty - h / 2;
        int gt = level.groundTop(tx);
        return (gt < 0 ? LEVEL_H - 2.0f : (float)gt) - h / 2;
    };
    auto target = [&](bool wantY) {   // x (or y) of a living player, alternating between the two
        float tx = x, ty = y;
        for (int i = 0; i < 2; ++i)
            if (players[i].alive()) {
                tx = players[i].cx(); ty = players[i].cy();
                if (attacks % 2 == i) break;
            }
        return wantY ? ty : std::max(camLeft + 2.0f, std::min(camRight - 2.0f, tx));
    };
    auto minions = [&]() {
        spawnTimer -= dt;
        if (spawnTimer <= 0 && era >= 1) { wantsSpawn = true; spawnTimer = 4.5f - era * 0.5f; }
    };
    if (state == 5) {   // dying, every kind
        y += dt * 0.5f;
        if (stateTime > 1.6f) { active = false; defeated = true; }
        return;
    }

    // ---------------------------------------------------------------- Alarm Clock
    if (kind == BK_ALARM) {
        switch (state) {
        case 0:   // drops in from above
            x += (hoverX - x) * 1.5f * dt;
            vy += 40 * dt;
            y += vy * dt;
            if (y >= groundY(x)) { y = groundY(x); vy = 0; state = 1; stateTime = 0; }   // entry: no shockwave
            break;
        case 1:   // stands, then picks a hop
            y = std::min(groundY(x), y + 20 * dt);
            minions();
            if (stateTime > (attacks % 3 == 0 ? 0.9f : 0.35f) - era * 0.03f) {
                float tx = target(false);
                tx = std::max(x - 9.0f, std::min(x + 9.0f, tx));
                fromX = x; fromY = y; toX = tx; toY = groundY(tx);
                state = 6; stateTime = 0;
            }
            break;
        case 6:   // crouch: the marker shows where it lands
            if (stateTime > 0.55f) { state = 2; stateTime = 0; }
            break;
        case 2: { // hop
            float k = std::min(1.0f, stateTime / 0.85f);
            x = fromX + (toX - fromX) * k;
            y = fromY + (toY - fromY) * k - 20.0f * k * (1 - k);
            if (k >= 1) {
                y = toY; landed = true; ++attacks;
                state = attacks % 3 == 0 ? 3 : 1; stateTime = 0;
            }
            break;
        }
        case 3:   // dizzy: stomp it now
            if (stateTime > 2.2f) { state = 1; stateTime = 0; }
            break;
        default: state = 1; stateTime = 0; break;
        }
        return;
    }

    // ---------------------------------------------------------------- Hourglass charges
    if (charging && (state == 6 || state == 2 || state == 3)) {
        switch (state) {
        case 6:   // line up with the target: the band shows the charge lane
            y += (toY - y) * 6 * dt;
            x = fromX + std::sin(t * 50) * 0.06f;
            if (stateTime > 0.8f - era * 0.04f) { state = 2; stateTime = 0; }
            break;
        case 2:   // charge left across the screen
            x -= (16.0f + era) * dt;
            if (x < camLeft + 1.8f) { state = 3; stateTime = 0; fromY = y; toY = groundY(x); }
            break;
        case 3:   // dizzy on the ground
            y = fromY + (toY - fromY) * ease(stateTime / 0.3f);
            if (stateTime > 2.2f) { state = 4; stateTime = 0; fromX = x; fromY = y; }
            break;
        }
        return;
    }

    switch (state) {
    case 0: {   // enter
        float k = ease(stateTime / 1.6f);
        x = fromX + (hoverX - fromX) * k;
        y = fromY + (4.0f - fromY) * k;
        if (stateTime > 1.6f) { state = 1; stateTime = 0; }
        break;
    }
    case 1: {   // hover, follow the camera, spit minions
        x += (hoverX + std::sin(t * 0.9f) * 3 - x) * 2.0f * dt;
        y = 4.0f + std::sin(t * 2.0f) * 0.6f;
        minions();
        if (stateTime > 2.6f - era * 0.25f) {
            ++attacks;
            charging = attacks % 2 == 0;   // swoop, charge, swoop, charge...
            fromX = x; fromY = y;
            stateTime = 0;
            state = 6;
            if (charging) {
                toY = std::max(2.5f, std::min(groundY(x), target(true)));
                toX = camLeft;
            } else {
                float tx = target(false);
                toX = tx;
                toY = groundY(tx);
            }
        }
        break;
    }
    case 6:     // aim: a warning marker shows where it will land
        x += (fromX - x) * 0.5f * dt;
        y = fromY + std::sin(t * 30) * 0.05f;
        if (stateTime > 0.85f - era * 0.08f) { state = 2; stateTime = 0; fromX = x; fromY = y; }
        break;
    case 2: {   // swoop down
        float k = ease(stateTime / 0.75f);
        x = fromX + (toX - fromX) * k;
        y = fromY + (toY - fromY) * k * k;
        if (stateTime > 0.75f) { state = 3; stateTime = 0; }
        break;
    }
    case 3:     // resting on the ground: stomp it now
        if (stateTime > 2.0f - era * 0.2f) { state = 4; stateTime = 0; fromX = x; fromY = y; }
        break;
    case 4: {   // rise again
        float k = ease(stateTime / 0.9f);
        y = fromY + (4.0f - fromY) * k;
        x += (hoverX - x) * 2.5f * dt;
        if (stateTime > 0.9f) { state = 1; stateTime = 0; charging = false; }
        break;
    }
    }
}
