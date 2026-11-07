#include "enemies/Enemies.h"
#include <algorithm>
#include <cmath>
#include "core/Config.h"
#include "core/Events.h"
#include "core/Game.h"
#include "core/Strings.h"

namespace {
void walk(Enemy& e, float dt, const Level& level, float speed) {
    e.vel.x = e.dir * speed;
    e.vel.y -= 30.0f * dt;
    if (e.vel.y < -20.0f) e.vel.y = -20.0f;

    e.pos.x += e.vel.x * dt;
    if (level.overlapsSolid(EnemyManager::box(e), -1)) {
        e.pos.x -= e.vel.x * dt;
        e.dir = -e.dir;
    }
    e.pos.y += e.vel.y * dt;
    if (level.overlapsSolid(EnemyManager::box(e), -1)) {
        float hh = EnemyManager::halfSize(e).y;
        if (e.vel.y < 0.0f) e.pos.y = std::floor(e.pos.y - hh) + 1.0f + hh;
        else e.pos.y -= e.vel.y * dt;
        e.vel.y = 0.0f;
    }
}
}

Vector2 EnemyManager::halfSize(const Enemy& e) {
    switch (e.type) {
        case 'B': return {0.75f, 0.75f};
        case 'G': return {0.38f, 0.38f};
        case 'Q': return e.awake ? Vector2{0.4f, 0.4f} : Vector2{0.3f, 0.3f};
        default: return {0.45f, 0.42f};
    }
}

Rectangle EnemyManager::box(const Enemy& e) {
    Vector2 h = halfSize(e);
    return {e.pos.x - h.x, e.pos.y - h.y, h.x * 2.0f, h.y * 2.0f};
}

void EnemyManager::clear() {
    enemies.clear();
    coins.clear();
    flags.clear();
    popups.clear();
}

void EnemyManager::spawn(char type, Vector2 bottomCentre, bool awake) {
    Enemy e;
    e.type = type;
    e.awake = awake;
    e.hp = type == 'B' ? Config::getInt("enemies", "brute_hp", 2) : 1;
    e.pos = {bottomCentre.x, bottomCentre.y + halfSize(e).y};
    e.home = e.pos;
    enemies.push_back(e);
}

void EnemyManager::bumpKill(int tileX, int tileTopY, Game& game) {
    for (Enemy& e : enemies) {
        if (!e.alive || e.stomped) continue;
        float bottom = e.pos.y - halfSize(e).y;
        if (std::fabs(e.pos.x - (tileX + 0.5f)) < 0.9f && std::fabs(bottom - tileTopY) < 0.3f) {
            e.stomped = true;
            e.deathTimer = 0.5f;
            game.addScore(200, {e.pos.x, e.pos.y + 0.8f});
        }
    }
}

void EnemyManager::update(float dt, Game& game) {
    const int world = game.world;

    // Turn new chunk markers into things.
    for (const SpawnMarker& m : game.level.newMarkers) {
        switch (m.type) {
            case 'C': case 'F': coins.push_back({{m.x + 0.5f, m.y + 0.5f}, false}); break;
            case 'P': {
                Flag f;
                f.x = f.targetX = m.x + 0.5f;
                f.baseY = m.y;
                flags.push_back(f);
                break;
            }
            default: spawn(m.type, {m.x + 0.5f, m.y}, false); break;
        }
    }
    game.level.newMarkers.clear();

    const float wakeX = game.camera.rightEdge() + 2.0f;
    const float walkerSpeed = Config::getFloat("enemies", "walker_speed", 2.0f) * (1.0f + std::min(world, 12) * 0.08f);
    const float bruteSpeed = Config::getFloat("enemies", "brute_speed", 1.2f);
    const float mimicSpeed = Config::getFloat("enemies", "mimic_speed", 3.5f);
    const float mimicWake = Config::getFloat("enemies", "mimic_wake_distance", 2.5f);
    const float flyerAmp = Config::getFloat("enemies", "flyer_amplitude", 1.5f);
    const float flyerDrift = Config::getFloat("enemies", "flyer_drift", 0.8f);

    for (Enemy& e : enemies) {
        if (!e.alive) continue;
        e.t += dt;
        if (e.stomped) {
            e.deathTimer -= dt;
            if (e.deathTimer <= 0.0f) e.alive = false;
            continue;
        }
        if (e.flash > 0.0f) e.flash -= dt;
        if (!e.awake && e.type != 'Q' && e.pos.x < wakeX) e.awake = true;

        // Nearest living player, for mimics.
        int nearest = -1;
        float best = 1e9f;
        for (int p = 0; p < 2; ++p) {
            const Player& pl = game.players[p];
            if (!pl.active || pl.dead) continue;
            Vector2 c = pl.centre();
            float d = std::hypot(c.x - e.pos.x, c.y - e.pos.y);
            if (d < best) { best = d; nearest = p; }
        }

        switch (e.type) {
            case 'S': if (e.awake) walk(e, dt, game.level, walkerSpeed); break;
            case 'B': if (e.awake) walk(e, dt, game.level, bruteSpeed); break;
            case 'G':
                if (e.awake) {
                    e.home.x -= flyerDrift * dt;
                    e.pos.x = e.home.x;
                    e.pos.y = e.home.y + flyerAmp * std::sin(e.t * 2.2f);
                }
                break;
            case 'Q':
                if (!e.awake) {
                    if (nearest >= 0 && best < mimicWake) {
                        e.awake = true;
                        e.vel.y = 9.0f;   // leaps out at you
                        game.enemies.popText({e.pos.x, e.pos.y + 1.0f}, Strings::get("mimic_text"));
                    }
                } else {
                    if (nearest >= 0) e.dir = game.players[nearest].centre().x < e.pos.x ? -1 : 1;
                    walk(e, dt, game.level, mimicSpeed);
                }
                break;
            default: break;
        }
        if (e.pos.y < -6.0f) e.alive = false;

        // Player contact: stomp from above, otherwise the player dies.
        for (Player& p : game.players) {
            if (!p.active || p.dead) continue;
            Rectangle pb = p.box();
            Rectangle eb = box(e);
            eb.x += 0.08f; eb.width -= 0.16f; eb.height -= 0.08f;
            if (!CheckCollisionRecs(pb, eb)) continue;
            bool stomp = p.vel.y <= 0.0f && p.pos.y >= e.pos.y - 0.05f;
            if (stomp) {
                p.bounce();
                if (--e.hp <= 0) {
                    e.stomped = true;
                    e.deathTimer = 0.5f;
                    game.addScore(200, {e.pos.x, e.pos.y + 0.8f});
                    events().enemyStomped.emit();
                } else {
                    e.flash = 0.3f;
                }
                break;
            }
            p.kill();
        }
    }

    // Coins.
    for (Coin& c : coins) {
        if (c.taken) continue;
        for (const Player& p : game.players) {
            if (!p.active || p.dead) continue;
            Vector2 pc = p.centre();
            if (std::hypot(pc.x - c.pos.x, pc.y - c.pos.y) < 0.7f) {
                c.taken = true;
                game.addCoin(c.pos, false);
                break;
            }
        }
    }

    // Flags. World 1 is honest. Then they run once, from world 4 twice, in endless worlds three times.
    const float runDistance = Config::getFloat("enemies", "flag_run_distance", 4.5f);
    const int maxRuns = world == 0 ? 0 : world < 3 ? 1 : world < 5 ? 2 : 3;
    for (Flag& f : flags) {
        if (f.taken) {
            f.dropT = std::min(1.0f, f.dropT + dt * 1.2f);
            continue;
        }
        float step = 22.0f * dt;
        f.x = f.x < f.targetX ? std::min(f.targetX, f.x + step) : f.targetX;
        bool settled = std::fabs(f.x - f.targetX) < 0.001f;

        for (int i = 0; i < 2; ++i) {
            const Player& p = game.players[i];
            if (!p.active || p.dead) continue;
            float dx = f.x - p.centre().x;
            if (settled && f.runs < maxRuns && dx > 0.6f && dx < runDistance) {
                f.targetX += f.runs == 0 ? 7.0f : 6.0f - f.runs;   // 7, 5, 4: stays on the flag chunk
                ++f.runs;
                popText({f.x, f.baseY + 8.8f}, Strings::get("flag_run_" + std::to_string(GetRandomValue(0, 3))));
                break;
            }
            if (p.pos.x + Player::W > f.x - 0.15f && p.pos.x < f.x + 0.15f && p.pos.y < f.baseY + 8.5f) {
                f.taken = true;
                game.reachFlag(f.x, p.pos.y - f.baseY);
                return;   // the era just changed; finish next step
            }
        }
    }

    for (Popup& p : popups) p.t += dt;

    // Forget anything left behind the level's first chunk.
    float minX = (float)game.level.beginX();
    coins.erase(std::remove_if(coins.begin(), coins.end(),
                               [minX](const Coin& c) { return c.taken || c.pos.x < minX; }),
                coins.end());
    enemies.erase(std::remove_if(enemies.begin(), enemies.end(),
                                 [minX](const Enemy& e) { return !e.alive || e.pos.x < minX; }),
                  enemies.end());
    flags.erase(std::remove_if(flags.begin(), flags.end(), [minX](const Flag& f) { return f.x < minX - 10.0f; }),
                flags.end());
    popups.erase(std::remove_if(popups.begin(), popups.end(), [](const Popup& p) { return p.t > 0.9f; }),
                 popups.end());
}
