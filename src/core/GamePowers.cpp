// GamePowers.cpp: the bosses' powers over time (see enemies/BossPowers.h). Camilla's Claude.
#include "core/Config.h"
#include "core/Game.h"
#include "core/Strings.h"

#include <algorithm>
#include <cmath>

const char* difficultyId(int d)
{
    static const char* ids[DIFF_COUNT] = {"gentle", "medium", "hard"};
    return ids[std::max(0, std::min(DIFF_COUNT - 1, d))];
}

int difficultyFromString(const std::string& s, int fallback)
{
    for (int d = 0; d < DIFF_COUNT; ++d)
        if (s == difficultyId(d) || s == std::to_string(d)) return d;
    return fallback;
}

namespace {
bool hasActivePower(int kind) { return kind == 0 || kind == 2 || kind == 3 || kind == 5 || kind == 6; }
bool overlapBox(float ax, float ay, float aw, float ah, float bx, float by, float bw, float bh)
{
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}
}  // namespace

const PowerTuning& Game::powerTuning() const { return POWER_TUNING[std::max(0, std::min(DIFF_COUNT - 1, difficulty))]; }

void Game::startBossPowers()
{
    powers.reset();
    powers.timer = powerTuning().every * 0.6f;   // the first power comes a little sooner
    powers.healsLeft = powerTuning().heals;
    powers.lastHp = boss.hp;
}

bool Game::bossHeldByPower() const
{
    return powers.warnT > 0 || powers.pullT > 0 || (powers.freezeT > 0 && !powerTuning().attackWhileFrozen);
}

// Where each player was over the last few seconds, for the Forgetting's pull back.
void Game::recordHistory(float dt)
{
    historyClock_ += dt;
    if (historyClock_ < 0.1f) return;
    historyClock_ = 0;
    for (int p = 0; p < 2; ++p) {
        if (!players[p].joined) { history_[p].clear(); continue; }
        history_[p].push_back({players[p].x, players[p].y});
        if (history_[p].size() > 60) history_[p].erase(history_[p].begin());
    }
}

void Game::clearHistory()
{
    for (auto& h : history_) h.clear();
    historyClock_ = 0;
}

void Game::applyBossPower()
{
    const PowerTuning& tu = powerTuning();
    const int k = boss.kind;
    powers.label = T("power_name_" + std::to_string(k));
    powers.labelT = 2.0f;
    powers.timer = tu.every;
    audio.play("unlock");
    if (opt.autoplay) TraceLog(LOG_INFO, "EVENT t=%.1f boss power %d (%s)", time, k, difficultyId(difficulty));
    cam.addShake(0.5f);
    switch (k) {
    case 0:   // the clock stops time: the players freeze where they stand
        powers.freezeT = tu.freeze;
        for (auto& pl : players)
            if (pl.alive()) particles.burst(pl.cx(), pl.cy(), 16, Color{235, 235, 225, 255}, 4, 0.5f, 0.12f);
        break;
    case 2:   // the bell: everything runs fast
        powers.hurryT = tu.hurryT;
        powers.hurryK = tu.hurryK;
        particles.burst(boss.x, boss.y, 40, Color{255, 220, 120, 255}, 9, 0.6f, 0.18f);
        break;
    case 3:   // the storm: time drags in the rain
        powers.rainT = tu.rainT;
        powers.rainK = tu.rainK;
        break;
    case 5: { // the scarecrow: the day races by, crows come fast
        powers.rushT = tu.rushT;
        powers.rushK = tu.rushK;
        for (int i = 0; i < tu.crows; ++i) {
            Enemy e = makeEnemy(EN_FLYER, boss.x + (i - tu.crows / 2.0f) * 1.5f, boss.y - 1.0f, tier(), etune);
            e.baseY = e.y;
            enemies.push_back(e);
        }
        particles.burst(boss.x, boss.y, 30, Color{240, 200, 110, 255}, 7, 0.6f, 0.16f);
        break;
    }
    case 6: { // the Forgetting pulls the players back a few seconds
        const int back = (int)std::lround(tu.pullBack * 10);
        for (int p = 0; p < 2; ++p) {
            pullPath_[p].clear();
            Player& pl = players[p];
            if (!pl.alive() || history_[p].size() < 2) continue;
            int idx = std::max(0, (int)history_[p].size() - 1 - back);
            // never back towards the Forgetting Wall: keep a clear six tiles from it
            while (idx < (int)history_[p].size() - 1 && history_[p][idx].x < wallX + 6.0f) ++idx;
            pl.invuln = std::max(pl.invuln, kPullTime + 0.3f);   // nothing hurts you while you are pulled
            pullPath_[p].push_back({pl.x, pl.y});
            for (int i = (int)history_[p].size() - 1; i >= idx; --i) pullPath_[p].push_back(history_[p][i]);
            particles.burst(pl.cx(), pl.cy(), 24, Color{200, 150, 255, 255}, 6, 0.6f, 0.14f);
        }
        powers.pullT = kPullTime;
        break;
    }
    default: break;
    }
}

void Game::updateBossPowers(float dt)
{
    const PowerTuning& tu = powerTuning();
    if (!boss.active || boss.state == 5) {
        powers.reset();   // the fight is over: every power ends with it
        return;
    }
    powers.labelT = std::max(0.0f, powers.labelT - dt);
    powers.freezeT = std::max(0.0f, powers.freezeT - dt);
    powers.rainT = std::max(0.0f, powers.rainT - dt);
    powers.rushT = std::max(0.0f, powers.rushT - dt);
    const int k = boss.kind;

    // ---- announce, then use, an active power while the boss hovers
    if (powers.warnT > 0) {
        powers.warnT -= dt;
        if (powers.warnT <= 0) { powers.warnT = 0; applyBossPower(); }
    } else if (hasActivePower(k) && boss.state == 1 && powers.freezeT <= 0 && powers.hurryT <= 0 &&
               powers.rainT <= 0 && powers.rushT <= 0 && powers.pullT <= 0) {
        powers.timer -= dt;
        if (powers.timer <= 0) {
            powers.warnT = tu.warn;
            powers.label = T("power_warn_" + std::to_string(k));
            powers.labelT = tu.warn;
            audio.play("slow");
        }
    }

    // ---- the Forgetting's pull back: the players slide back along where they have been
    if (powers.pullT > 0) {
        powers.pullT = std::max(0.0f, powers.pullT - dt);
        float a = 1.0f - powers.pullT / kPullTime;
        for (int p = 0; p < 2; ++p) {
            auto& path = pullPath_[p];
            if (path.size() < 2) continue;
            float f = a * (path.size() - 1);
            size_t i = std::min(path.size() - 2, (size_t)f);
            float u = f - i;
            players[p].x = path[i].x + (path[i + 1].x - path[i].x) * u;
            players[p].y = path[i].y + (path[i + 1].y - path[i].y) * u;
            players[p].vx = players[p].vy = 0;
        }
        if (powers.pullT <= 0) {
            for (int p = 0; p < 2; ++p)
                if (pullPath_[p].size() >= 2) { players[p].invuln = std::max(players[p].invuln, 1.0f); pullPath_[p].clear(); }
            clearHistory();
        }
    }

    // ---- the Empty Shirt rewinds a hit if you don't follow up quickly
    if (boss.hp < powers.lastHp) powers.sinceHit = 0;
    powers.lastHp = boss.hp;
    if (powers.sinceHit >= 0) powers.sinceHit += dt;
    if (k == 1 && powers.sinceHit >= tu.healWindow && powers.healsLeft > 0 && boss.hp > 0 && boss.hp < boss.maxHp) {
        boss.hp += 1;
        powers.lastHp = boss.hp;
        powers.healsLeft--;
        powers.sinceHit = -1;
        powers.label = T("power_name_1");
        powers.labelT = 2.0f;
        audio.play("revive");
        particles.burst(boss.x, boss.y, 30, Color{230, 225, 210, 255}, 5, 0.6f, 0.15f);
        if (opt.autoplay) TraceLog(LOG_INFO, "EVENT t=%.1f boss power 1 rewinds to %d hp", time, boss.hp);
    } else if (k == 1 && powers.sinceHit >= tu.healWindow) {
        powers.sinceHit = -1;
    }

    // ---- the Last Train: record each charge, then send its echoes down the same line
    if (k == 4) {
        if (boss.state == 2) powers.curPath.push_back({boss.x, boss.y});
        if (boss.landed && !powers.curPath.empty()) {
            for (int i = 0; i < tu.echoes; ++i) {
                TrainEcho e;
                e.path = powers.curPath;
                e.delay = 0.6f * (i + 1);
                powers.echoes.push_back(e);
            }
            if (opt.autoplay) TraceLog(LOG_INFO, "EVENT t=%.1f boss power 4 echoes x%d", time, tu.echoes);
            powers.curPath.clear();
            powers.label = T("power_name_4");
            powers.labelT = 1.6f;
        }
        if (boss.state != 2 && !boss.landed) powers.curPath.clear();
    }
    for (auto& e : powers.echoes) {
        e.t += dt;
        if (!e.active()) continue;
        Vector2 c = e.pos();
        float bw = boss.w * 0.85f, bh = boss.h * 0.85f;
        for (int p = 0; p < 2; ++p) {
            Player& pl = players[p];
            if (!pl.alive() || !overlapBox(pl.x, pl.y, pl.w, pl.h, c.x - bw / 2, c.y - bh / 2, bw, bh)) continue;
            if (tu.echoHurts) {
                if (pl.invuln <= 0 && pl.dashTime <= 0) hurtPlayer(p, false);
            } else {
                pl.vx = (pl.cx() < c.x ? -1.0f : 1.0f) * 9.0f;
                pl.vy = std::min(pl.vy, -5.0f);
                pl.wallLock = 0.2f;
            }
        }
    }
    powers.echoes.erase(std::remove_if(powers.echoes.begin(), powers.echoes.end(), [](const TrainEcho& e) {
        return e.t > e.delay + e.path.size() / 60.0f + 0.2f;
    }), powers.echoes.end());

    // ---- the storm's rain, while time drags
    if (powers.rainT > 0)
        for (int i = 0; i < 3; ++i)
            particles.burst(randf(cam.left(), cam.right()), cam.top - 0.5f, 1, Color{150, 190, 255, 200}, 1.5f, 1.0f, 0.07f, 40);
}
