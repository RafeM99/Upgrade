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
void Boss::start(float camX, int era, int hpBase, int hpPerEra, int kind_)
{
    active = true;
    kind = kind_;
    landed = false;
    defeated = false;
    maxHp = hp = hpBase + era * hpPerEra;
    state = 0;
    stateTime = 0;
    t = 0;
    hurt = 0;
    spawnTimer = 3;
    x = camX + 16;
    y = -3;
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

    switch (state) {
    case 0: {   // enter from the top right
        float k = ease(stateTime / 1.6f);
        x = fromX + (hoverX - fromX) * k;
        y = fromY + (4.0f - fromY) * k;
        if (stateTime > 1.6f) { state = 1; stateTime = 0; }
        break;
    }
    case 1: {   // hover, follow the camera, spit minions
        if (kind == 0) {
            // the clock ticks: it jumps from place to place twice a second
            if (std::fmod(t, 0.5f) < dt) x += (hoverX + std::sin(t * 0.9f) * 3 - x) * 0.6f;
            y = 4.0f + (((int)(t * 2)) % 2 ? 0.3f : -0.3f);
        } else if (kind == 1) {
            // the shirt flutters wide and slow, like washing in the wind
            x += (hoverX + std::sin(t * 0.6f) * 5 - x) * 1.5f * dt;
            y = 4.0f + std::sin(t * 1.3f) * 1.0f;
        } else {
            x += (hoverX + std::sin(t * 0.9f) * 3 - x) * 2.0f * dt;
            y = 4.0f + std::sin(t * 2.0f) * 0.6f;
        }
        spawnTimer -= dt;
        if (spawnTimer <= 0 && era >= 1) { wantsSpawn = true; spawnTimer = (4.5f - era * 0.5f) * (kind == 5 ? 0.7f : 1.0f); }
        if (stateTime > 2.6f - era * 0.25f) {
            // pick a target player and swoop
            float tx = x;
            for (int i = 0; i < 2; ++i)
                if (players[i].alive()) { tx = players[i].cx(); if ((int)(t * 10) % 2 == i) break; }
            tx = std::max(camLeft + 2.0f, std::min(camRight - 2.0f, tx));
            int gt = level.groundTop((int)std::floor(tx));
            fromX = x; fromY = y;
            toX = tx;
            toY = (gt < 0 ? LEVEL_H - 2.0f : (float)gt) - h / 2;
            state = 6;
            stateTime = 0;
        }
        break;
    }
    case 6:     // aim: a warning marker shows where it will land
        x += (fromX - x) * 0.5f * dt;
        y = fromY + std::sin(t * 30) * 0.05f;
        // the storm takes longer to aim, then strikes much faster
        if (stateTime > 0.85f - era * 0.08f + (kind == 3 ? 0.35f : 0.0f)) { state = 2; stateTime = 0; fromX = x; fromY = y; }
        break;
    case 2: {   // swoop down
        float k = std::min(1.0f, stateTime / swoopLen());
        if (kind == 4) {
            // the train drops onto the line where it is, then charges along the ground
            if (k < 0.3f) { float d = ease(k / 0.3f); x = fromX; y = fromY + (toY - fromY) * d; }
            else { x = fromX + (toX - fromX) * ease((k - 0.3f) / 0.7f); y = toY; }
        } else if (kind == 1) {
            // the shirt glides in on the wind
            x = fromX + (toX - fromX) * std::sin(k * 1.5707963f);
            y = fromY + (toY - fromY) * k * k * k;
        } else {
            float e = ease(k);
            x = fromX + (toX - fromX) * e;
            y = fromY + (toY - fromY) * e * e;
        }
        if (stateTime > swoopLen()) { state = 3; stateTime = 0; landed = true; }
        break;
    }
    case 3:     // resting on the ground: stomp it now (the bell rests a little longer after it rings)
        if (stateTime > 2.0f - era * 0.2f + (kind == 2 ? 0.4f : 0.0f)) { state = 4; stateTime = 0; fromX = x; fromY = y; }
        break;
    case 4: {   // rise again
        float k = ease(stateTime / 0.8f);
        y = fromY + (4.0f - fromY) * k;
        x += (hoverX - x) * 1.5f * dt;
        if (stateTime > 0.8f) { state = 1; stateTime = 0; }
        break;
    }
    case 5:     // dying
        y += dt * 0.5f;
        if (stateTime > 1.6f) { active = false; defeated = true; }
        break;
    }
}
