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

// ------------------------------------------------------------------ Boss (akshat, boss rework)
// The fight loop, the same for every boss:
//   HOVER  it moves about and throws time-themed shots. Attack a shot as it reaches you to DEFLECT
//          it; a deflected shot flies back and knocks the boss dizzy (STUN).
//   AIM    its big attack is telegraphed (a marker on the floor, the train's lane and whistle).
//   ATTACK the big attack. If it hurts nobody, the boss is stuck for a moment (STUN); if it hurt
//          someone it just recovers (no opening).
//   STUN   the weak point glows: hit it with ATTACK. Both players hitting it within a moment of
//          each other is "Remember together", a much bigger hit. A few hits and it shakes free.
// From half health (phase 2) it is faster; the Forgetting slams twice.
namespace {
float easeK(float k) { k = std::min(1.0f, std::max(0.0f, k)); return k * k * (3 - 2 * k); }
const Player* nearestTo(const Player* ps, float x, float y)
{
    float d;
    return nearest(ps, x, y, d);
}
// a point of the bell, turned by its tilt about its centre (y down: positive tilt turns clockwise)
void turn(float lx, float ly, float a, float& ox, float& oy)
{
    ox = lx * std::cos(a) - ly * std::sin(a);
    oy = lx * std::sin(a) + ly * std::cos(a);
}
HitBox around(float cx, float cy, float w, float h) { return {cx - w / 2, cy - h / 2, w, h}; }
}  // namespace

void Boss::start(float camX, int era, int hpTotal, int kind_, const FightTuning& tuning, float floor, float aL, float aR)
{
    (void)camX; (void)era;
    active = true;
    kind = kind_;
    ft = tuning;
    floorY = floor;
    arenaL = aL;
    arenaR = aR;
    landed = defeated = charge = false;
    maxHp = hp = std::max(1, hpTotal);
    state = BS_ENTER;
    stateTime = t = hurt = 0;
    spawnTimer = ft.minionEvery > 0 ? ft.minionEvery : 1e9f;
    phase = 1;
    shots.clear();
    shotTimer = 1.6f;
    shotCount = 0;
    stunT = 0;
    stunHits = 0;
    side = 1;
    face = -1;
    slamsLeft = 0;
    tilt = 0;
    deflectT = 0;
    if (kind == 2) { w = 2.5f; h = 2.9f; }
    else if (kind == 4) { w = 2.8f; h = 2.1f; }
    else if (kind == 6) { w = 3.9f; h = 4.6f; }
    else { w = 2.4f; h = 2.2f; }
    x = kind == 4 ? arenaR - 1.8f : arenaR + 3;
    y = -3;
    fromX = x; fromY = y;
    toX = kind == 4 ? x : (arenaL + arenaR) / 2 + 3;
    toY = hoverY();
}

float Boss::groundY() const
{
    // where its centre sits when it is down on the arena floor, from its art
    if (kind == 2) return floorY - 1.18f;   // the bell's clapper just above the floor
    if (kind == 4) return floorY - 1.12f;   // the train's wheels on the line
    if (kind == 6) return floorY - 2.15f;   // the Forgetting's splayed tentacles on the floor
    return floorY - h / 2;
}

float Boss::hoverY() const
{
    if (kind == 4) return groundY();         // the train never leaves the line
    if (kind == 6) return floorY - 5.6f;
    return floorY - 5.0f;
}

int Boss::bodyBoxes(HitBox out[3]) const
{
    if (kind == 2) {
        // the School Bell (stage 2 sheet, 3x3 tiles drawn 0.2 high): the dome, then the wide rim.
        // The handle and the clapper are left out (the clapper is the weak point).
        float dx, dy;
        turn(0.0f, -0.45f, tilt, dx, dy);
        out[0] = around(x + dx, y + dy, 1.5f, 1.5f);
        turn(0.0f, 0.5f, tilt, dx, dy);
        out[1] = around(x + dx, y + dy, 2.4f, 0.42f);
        return 2;
    }
    if (kind == 4) {
        // the Last Train (stage 4 sheet): the boiler and cab from the roof to the wheels, not the smoke
        out[0] = {x - 1.32f, y - 0.86f, 2.64f, 1.94f};
        return 1;
    }
    if (kind == 6) {
        // the Forgetting (Blender model at 1.8x, centre 0.4 under the dome's pivot): the dome with
        // its skirt, and the tentacles under it (splayed wide and short when they slam the floor)
        out[0] = {x - 1.55f, y - 1.95f, 3.1f, 2.7f};
        bool splay = state == BS_STUN || (state == BS_ATTACK && stateTime > swoopLen() * 0.6f);
        out[1] = splay ? HitBox{x - 2.4f, y + 0.75f, 4.8f, 1.35f} : HitBox{x - 1.8f, y + 0.75f, 3.6f, 1.9f};
        return 2;
    }
    out[0] = around(x, y, w, h);
    return 1;
}

HitBox Boss::weakBox() const
{
    if (kind == 2) {   // the clapper hanging under the bell: it swings out when the bell tips over
        float dx, dy;
        turn(0.09f, 0.93f, tilt, dx, dy);
        return around(x + dx, y + dy, 0.8f, 0.7f);
    }
    if (kind == 4) return around(x + 0.1f, y - 0.05f, 1.35f, 1.35f);   // the glowing clock face on the front
    if (kind == 6) {   // the huge eye, turned to the players (where the 3D model really shows it)
        if (eyeSet) return around(x + eyeDX, y + eyeDY, 1.15f, 1.15f);
        return around(x + face * 0.95f, y - 0.62f, 1.15f, 1.15f);
    }
    return around(x, y, 1.2f, 1.2f);
}

HitBox Boss::bounds() const
{
    HitBox b[3];
    int n = bodyBoxes(b);
    float x0 = b[0].x, y0 = b[0].y, x1 = b[0].x + b[0].w, y1 = b[0].y + b[0].h;
    for (int i = 1; i < n; ++i) {
        x0 = std::min(x0, b[i].x); y0 = std::min(y0, b[i].y);
        x1 = std::max(x1, b[i].x + b[i].w); y1 = std::max(y1, b[i].y + b[i].h);
    }
    return {x0, y0, x1 - x0, y1 - y0};
}

void Boss::stun(float seconds)
{
    const bool state_before_stun_was_attack = state == BS_ATTACK;
    state = BS_STUN;
    stateTime = 0;
    stunT = stunLen = seconds;
    stunHits = 0;
    coopDone = false;
    charge = false;
    // dizzy from a deflected shot (not stuck after its own attack): it answers with the big attack
    retaliate = state_before_stun_was_attack ? false : true;
    fromX = x; fromY = y;
    wantsStunSound = true;
}

void Boss::update(float dt, float camLeft, float camRight, const Level& level, const Player* players, int era, float timeScale)
{
    (void)camLeft; (void)camRight; (void)level; (void)era;
    if (!active) return;
    dt *= timeScale;
    t += dt;
    stateTime += dt;
    hurt = std::max(0.0f, hurt - dt);
    deflectT = std::max(0.0f, deflectT - dt);
    wantsSpawn = landed = false;
    phase = hp * 2 <= maxHp ? 2 : 1;
    const float pk = phase == 2 ? 0.72f : 1.0f;   // phase 2: everything comes sooner

    // who it is after: the nearest player (and the middle of the two, for where it hovers)
    const Player* tgt = nearestTo(players, x, y);
    float midX = (arenaL + arenaR) / 2;
    {
        int n = 0;
        float sx = 0;
        for (int i = 0; i < 2; ++i) if (players[i].alive()) { sx += players[i].cx(); ++n; }
        if (n) midX = sx / n;
    }
    if (tgt && state != BS_STUN) face = tgt->cx() < x ? -1 : 1;
    const float lo = arenaL + 2.2f, hi = arenaR - 2.2f;
    auto clampX = [&](float v) { return std::max(lo, std::min(hi, v)); };
    const float stationX = side > 0 ? arenaR - 1.8f : arenaL + 1.8f;

    auto fire = [&]() {
        if (!tgt) return;
        const float sk = ft.shotSpeed;
        const float px = tgt->cx(), py = tgt->cy();
        auto aimed = [&](int type, float sx, float sy, float speed, float r, float spread) {
            BossShot s;
            s.type = type; s.x = sx; s.y = sy; s.r = r;
            float a = std::atan2(py - sy, px - sx) + spread;
            s.vx = std::cos(a) * speed; s.vy = std::sin(a) * speed;
            shots.push_back(s);
        };
        if (kind == 2) {
            // the bell swings and rings: a ring rolling along the floor, then one through the air
            if (shotCount % 2 == 0) {
                BossShot s;
                s.type = SHOT_RING_GROUND; s.r = 0.45f; s.x = x; s.y = floorY - s.r;
                s.vx = (px < x ? -1.0f : 1.0f) * 5.5f * sk;
                shots.push_back(s);
            }
            if (shotCount % 2 == 1) {
                // through the air, never steeper than about 40 degrees: always something you can swing at
                aimed(SHOT_RING_AIR, x, y + 0.9f, 6.0f * sk, 0.42f, 0);
                BossShot& s = shots.back();
                float sp = std::hypot(s.vx, s.vy), dir = px < x ? -1.0f : 1.0f;
                float a = std::min(0.7f, std::atan2(std::max(0.0f, s.vy), std::fabs(s.vx)));
                s.vx = dir * std::cos(a) * sp;
                s.vy = std::sin(a) * sp;
            }
        } else if (kind == 4) {
            // the train puffs: a lump of coal lobbed at you, then steam along the line
            if (shotCount % 2 == 0 || phase == 2) {
                for (int i = 0; i < 2; ++i) {
                    const Player& pl = players[i];
                    if (!pl.alive() || (phase == 1 && &pl != tgt)) continue;   // phase 2: a lump for each of you
                    BossShot s;
                    s.type = SHOT_COAL; s.r = 0.32f; s.x = x - side * 0.6f; s.y = y - 1.3f;
                    const float T = 1.15f, g = 14.0f;
                    s.vx = (pl.cx() - s.x) / T;
                    s.vy = ((pl.cy() - 0.2f) - s.y - 0.5f * g * T * T) / T;
                    shots.push_back(s);
                }
            } else {
                BossShot s;
                s.type = SHOT_STEAM; s.r = 0.45f; s.x = x - side * 1.5f; s.y = floorY - 0.75f;
                s.vx = -side * 4.2f * sk;
                shots.push_back(s);
            }
        } else if (kind == 6) {
            // the Forgetting throws lost moments from its eye; in phase 2, three at once
            HitBox e = weakBox();
            aimed(SHOT_ORB, e.cx(), e.cy(), 4.2f * sk, 0.38f, 0);
            if (phase == 2) { aimed(SHOT_ORB, e.cx(), e.cy(), 4.2f * sk, 0.38f, 0.4f); aimed(SHOT_ORB, e.cx(), e.cy(), 4.2f * sk, 0.38f, -0.4f); }
        } else {
            aimed(SHOT_ORB, x, y, 4.5f * sk, 0.38f, 0);
        }
        for (auto& s : shots) if (s.id == 0) s.id = shotCount * 4 + 1 + (int)(&s - &shots[0]) % 4;
        ++shotCount;
        wantsShotSound = true;
    };

    auto beginAim = [&]() {
        // pick a target (alternating between the two players) and telegraph the big attack
        const Player* p = tgt;
        int pick = (int)(t * 10) % 2;
        if (players[pick].alive()) p = &players[pick];
        fromX = x; fromY = y;
        hitSomeone = false;
        if (kind == 4) {
            charge = true;
            toX = side > 0 ? arenaL + 1.8f : arenaR - 1.8f;   // the whole length of the line
            toY = groundY();
        } else {
            charge = false;
            toX = clampX(p ? p->cx() : x);
            if (kind == 2) toX = std::max(arenaL + 7.3f, std::min(arenaR - 7.3f, toX));   // the bell lands clear of the ledges
            toY = groundY();
        }
        state = BS_AIM;
        stateTime = 0;
        wantsWhistle = true;
    };

    switch (state) {
    case BS_ENTER: {
        float k = easeK(stateTime / 1.6f);
        if (kind == 4) { x = stationX; y = -3 + (groundY() + 3) * k * k; }
        else { x = fromX + (toX - fromX) * k; y = fromY + (toY - fromY) * k; }
        if (stateTime > 1.6f) {
            if (kind == 4) wantsSlam = true;
            state = BS_HOVER; stateTime = 0;
        }
        break;
    }
    case BS_HOVER: {
        if (kind == 2) {
            // the bell swings to and fro above the players
            x += (clampX(midX + std::sin(t * 0.8f) * 3.5f) - x) * 1.4f * dt;
            y = hoverY() + std::sin(t * 2.0f) * 0.35f;
            tilt = std::sin(t * 3.2f) * 0.18f;
        } else if (kind == 4) {
            x = stationX + std::sin(t * 18) * 0.03f;   // the engine chugs at its end of the line
            y = groundY();
            tilt = 0;
        } else {
            x += (clampX(midX + std::sin(t * 0.6f) * 4.0f) - x) * 1.2f * dt;
            y = hoverY() + std::sin(t * 1.6f) * 0.35f;
            tilt = 0;
        }
        if (!held) {
            shotTimer -= dt;
            if (shotTimer <= 0) { fire(); shotTimer = ft.shotEvery * pk; }
            if (ft.minionEvery > 0) {
                spawnTimer -= dt;
                if (spawnTimer <= 0) { wantsSpawn = true; spawnTimer = ft.minionEvery; }
            }
        }
        if (stateTime > ft.hoverTime * pk && !held) {
            slamsLeft = kind == 6 && phase == 2 ? 2 : 1;
            beginAim();
        }
        break;
    }
    case BS_AIM: {
        if (kind == 4) { x = stationX + std::sin(t * 40) * 0.06f; y = groundY(); }   // it shudders, steam up
        else {
            x += (toX - x) * std::min(1.0f, 2.5f * dt);
            y = fromY + std::sin(t * 30) * 0.05f;
            tilt = std::sin(t * 30) * 0.08f;
        }
        if (stateTime > ft.aimTime * (phase == 2 ? 0.85f : 1.0f)) { state = BS_ATTACK; stateTime = 0; fromX = x; fromY = y; }
        break;
    }
    case BS_ATTACK: {
        bool done = false;
        if (kind == 4) {
            // the charge: full speed along the line to the far end
            float dir = toX > fromX ? 1.0f : -1.0f;
            float speed = ft.trainSpeed * std::min(1.0f, 0.35f + stateTime * 2.5f);
            x += dir * speed * dt;
            y = groundY();
            if ((toX - x) * dir <= 0) { x = toX; done = true; }
        } else {
            float k = std::min(1.0f, stateTime / swoopLen());
            float e = easeK(k);
            x = fromX + (toX - fromX) * e;
            y = fromY + (toY - fromY) * e * e;
            tilt *= 0.9f;
            done = stateTime > swoopLen();
        }
        if (done) {
            landed = true;
            wantsSlam = true;
            if (kind == 2) {
                // the bell's landing sends a ring rolling each way along the floor
                for (int d = -1; d <= 1; d += 2) {
                    BossShot s;
                    s.type = SHOT_RING_GROUND; s.r = 0.45f; s.x = x + d * 1.3f; s.y = floorY - s.r;
                    s.vx = d * 5.0f * ft.shotSpeed;
                    shots.push_back(s);
                }
            }
            if (kind == 4) {
                side = -side;   // it slams into the buffers at the far end: stunned there
                stun(hitSomeone ? ft.stunMiss * 0.6f : ft.stunMiss);
            } else if (kind == 6 && --slamsLeft > 0) {
                state = BS_RISE; stateTime = 0; fromX = x; fromY = y;   // up, and straight into the second slam
            } else if (!hitSomeone) {
                stun(ft.stunMiss);   // it missed: stuck for a moment
            } else {
                state = BS_RECOVER; stateTime = 0;
            }
        }
        break;
    }
    case BS_STUN: {
        stunT -= dt;
        if (kind == 2) {
            // rung dizzy: it drops to the floor and tips over, its clapper swinging out
            y += (groundY() - y) * std::min(1.0f, 9.0f * dt);
            x += (std::max(arenaL + 7.3f, std::min(arenaR - 7.3f, x)) - x) * std::min(1.0f, 6.0f * dt);   // clear of the ledges
            float want = -face * 0.5f + std::sin(t * 7) * 0.05f;
            tilt += (want - tilt) * std::min(1.0f, 8.0f * dt);
        } else if (kind == 4) {
            y = groundY();
        } else if (kind == 6) {
            y += (groundY() - y) * std::min(1.0f, 8.0f * dt);   // tentacles stuck: the eye hangs low
        }
        if (stunHits >= ft.stunHits) stunT = std::min(stunT, 0.5f);   // enough: it shakes itself free
        if (stunT <= 0) {
            if (kind == 4) {
                state = BS_HOVER; stateTime = retaliate ? 99.0f : 0.0f; shotTimer = 0.8f;   // 99: the charge comes now
                wantsBlast = true;   // a blast of steam clears the line in front of it first
            } else {
                state = BS_RISE; stateTime = 0; fromX = x; fromY = y;
                slamsLeft = retaliate ? (kind == 6 && phase == 2 ? 2 : 1) : 0;   // rises straight into its big attack
            }
            retaliate = false;
        }
        break;
    }
    case BS_RECOVER:
        if (stateTime > 0.7f) {
            if (kind == 4) { state = BS_HOVER; stateTime = 0; }
            else { state = BS_RISE; stateTime = 0; fromX = x; fromY = y; slamsLeft = 0; }
        }
        break;
    case BS_RISE: {
        const float len = slamsLeft > 0 ? 0.45f : 0.8f;
        float k = easeK(stateTime / len);
        y = fromY + (hoverY() - fromY) * k;
        x += (clampX(midX) - x) * 1.5f * dt;
        tilt *= 0.9f;
        if (stateTime > len) {
            if (slamsLeft > 0) beginAim();   // straight into a big attack (a retaliation, or the Forgetting's second slam)
            else { state = BS_HOVER; stateTime = 0; shotTimer = std::min(shotTimer, 1.0f); }
        }
        break;
    }
    case BS_DYING:
        y += dt * 0.5f;
        shots.clear();
        if (stateTime > 1.6f) { active = false; defeated = true; }
        break;
    }

    // ---- shots
    for (auto& s : shots) {
        s.t += dt;
        if (s.reflected) {
            // back at the boss, homing on its middle
            HitBox b = bounds();
            float dx = b.cx() - s.x, dy = b.cy() - s.y, d = std::max(0.01f, std::hypot(dx, dy));
            s.vx = dx / d * 15.0f;
            s.vy = dy / d * 15.0f;
        } else if (s.type == SHOT_RING_GROUND) {
            s.y = floorY - s.r;
            s.vy = 0;
        } else if (s.type == SHOT_COAL) {
            s.vy += 14.0f * dt;
            if (s.y + s.r >= floorY && s.vy > 0) s.dead = true;   // it breaks on the floor
        } else if (s.type == SHOT_STEAM) {
            s.vy = std::sin(s.t * 5) * 0.4f;
        } else if (s.type == SHOT_ORB && s.t < 1.2f) {
            // a lost moment drifts after you for a moment, then flies straight
            const Player* p = nearestTo(players, s.x, s.y);
            if (p) {
                float dx = p->cx() - s.x, dy = p->cy() - s.y, d = std::max(0.01f, std::hypot(dx, dy));
                float sp = std::max(0.01f, std::hypot(s.vx, s.vy));
                s.vx += (dx / d * sp - s.vx) * 1.2f * dt;
                s.vy += (dy / d * sp - s.vy) * 1.2f * dt;
            }
        }
        s.x += s.vx * dt;
        s.y += s.vy * dt;
        // shots in flight break on ledges and walls (the ones rolling along the floor stay on it)
        if (!s.reflected && s.type != SHOT_RING_GROUND && s.type != SHOT_STEAM &&
            level.isSolidChar(level.tile((int)std::floor(s.x), (int)std::floor(s.y))))
            s.dead = true;
        if (s.x < arenaL - 2 || s.x > arenaR + 2 || s.y > floorY + 1 || s.y < -6 || s.t > 8) s.dead = true;
    }
    shots.erase(std::remove_if(shots.begin(), shots.end(), [](const BossShot& s) { return s.dead; }), shots.end());
}
