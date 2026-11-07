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

// The box an enemy keeps out of the terrain. It is a little bigger than the hitbox where the
// drawn sprite is (the art is drawn ENEMY_K bigger than the hitbox), so a blob's body or a
// book's pages never sink into a wall or a ceiling. The hitbox for players is unchanged.
void enemyTerrainBox(const Enemy& e, float& x, float& y, float& w, float& h)
{
    float padX = 0, padTop = 0, padBottom = 0;
    if (e.type == EN_BLOB) { padX = 0.1f; padTop = 0.22f; }
    else if (e.type == EN_FLYER) { padX = 0.15f; padTop = 0.12f; padBottom = 0.12f; }
    else if (e.type == EN_BRUTE) { padX = 0.0f; padTop = 0.12f; }
    x = e.x - padX;
    w = e.w + 2 * padX;
    y = e.y - padTop;
    h = e.h + padTop + padBottom;
}

static bool enemyHitsTerrain(const Level& L, const Enemy& e, float dx, float dy)
{
    float x, y, w, h;
    enemyTerrainBox(e, x, y, w, h);
    return solidE(L, x + dx, y + dy, w, h);
}

bool enemyMove(Enemy& e, const Level& level, float dx, float dy)
{
    // Move along one axis in small steps and stop flush against the first solid tile, so an
    // enemy never passes through a thin wall or sinks into the floor on a fast frame.
    float total = std::fabs(dx) + std::fabs(dy);
    if (total <= 0) return false;
    int steps = std::max(1, (int)std::ceil(total / 0.2f));
    float sx = dx / steps, sy = dy / steps;
    for (int i = 0; i < steps; ++i) {
        if (!enemyHitsTerrain(level, e, sx, sy)) { e.x += sx; e.y += sy; continue; }
        // blocked inside this step: find how far it can go (binary search to ~1/500 tile)
        float lo = 0, hi = 1;
        for (int k = 0; k < 9; ++k) {
            float mid = (lo + hi) / 2;
            if (enemyHitsTerrain(level, e, sx * mid, sy * mid)) hi = mid; else lo = mid;
        }
        e.x += sx * lo;
        e.y += sy * lo;
        return true;
    }
    return false;
}

bool settleEnemy(Enemy& e, const Level& level)
{
    // Spawned (or pushed) into the terrain: move it to the nearest free spot, trying up first,
    // then sideways, then both. Returns false if there is no room nearby at all.
    if (!enemyHitsTerrain(level, e, 0, 0)) return true;
    for (int r = 1; r <= 24; ++r) {
        float d = r * 0.25f;
        const float tries[][2] = {{0, -d}, {-d, 0}, {d, 0}, {-d, -d}, {d, -d}, {0, d}};
        for (auto& t : tries)
            if (!enemyHitsTerrain(level, e, t[0], t[1])) {
                e.x += t[0];
                e.y += t[1];
                e.baseY += t[1];
                return true;
            }
    }
    return false;
}

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
    e.blink = std::max(0.0f, e.blink - dt);
    float eraK = 1.0f + tu.speedPerEra * era;
    // pushed into a wall (a knock-back, a crumbling floor): step back out first
    if (enemyHitsTerrain(level, e, 0, 0) && !settleEnemy(e, level)) { e.remove = true; return; }

    if (e.type == EN_FLYER) {
        float d;
        const Player* p = nearest(players, e.cx(), e.cy(), d);
        float mx = 0;
        if (p && d < 12) {
            float dx = p->cx() - e.cx();
            if (std::fabs(dx) > 0.2f) e.dir = dx < 0 ? -1 : 1;
            mx = e.dir * tu.flyerSpeed * eraK * dt;
            e.baseY += (std::min(p->y - 1.5f, 10.0f) - e.baseY) * 0.3f * dt;
        }
        e.baseY = std::max(0.5f, e.baseY);
        // fly around the terrain, never through it: blocked sideways, it rises over the obstacle
        if (mx != 0 && enemyMove(e, level, mx, 0)) e.baseY -= 1.5f * dt;
        float wantY = std::max(0.5f, e.baseY + std::sin(e.t * 2.4f) * 1.1f);
        if (enemyMove(e, level, 0, wantY - e.y)) {
            // the sine wave hit a floor or a ceiling: move the centre of the wave away from it
            e.baseY += (wantY > e.y ? -1.0f : 1.0f) * 2.0f * dt;
        }
        // glitch: from 16-bit on it flickers now and then (drawn only, it never jumps)
        if (era >= 2 && std::fmod(e.t, 3.0f) < dt) e.blink = 0.25f;
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

    auto turn = [&]() {
        e.dir = -e.dir;
        if (e.state == 2) { e.state = 0; e.stateTime = 0; }
    };
    // turn at ledges before stepping off them (only on the ground: falling enemies keep going)
    if (e.onGround && e.vx != 0) {
        float bx, by, bw, bh;
        enemyTerrainBox(e, bx, by, bw, bh);
        float ahead = e.dir > 0 ? bx + bw + std::fabs(e.vx * dt) + 0.02f : bx - std::fabs(e.vx * dt) - 0.02f;
        if (!level.solidFor((int)std::floor(ahead), (int)std::floor(e.y + e.h + 0.1f), -1, false)) { turn(); e.vx = -e.vx; }
    }
    if (enemyMove(e, level, e.vx * dt, 0)) turn();
    bool wasGround = e.onGround;
    e.onGround = false;
    if (enemyMove(e, level, 0, e.vy * dt)) {
        if (e.vy > 0) e.onGround = true;
        e.vy = 0;
    } else if (wasGround && e.vy > 0 && enemyHitsTerrain(level, e, 0, 0.05f)) {
        e.onGround = true;   // resting exactly on the floor
        e.vy = 0;
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
            // The train charges along the ground only where the line is flat all the way from
            // where it drops to the target; anywhere else it swoops like the others.
            charge = false;
            if (kind == 4 && gt >= 0) {
                int a = (int)std::floor(std::min(x, tx)), b = (int)std::floor(std::max(x, tx));
                charge = true;
                for (int c = a; c <= b && charge; ++c) charge = level.groundTop(c) == gt;
            }
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
        if (kind == 4 && charge) {
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
