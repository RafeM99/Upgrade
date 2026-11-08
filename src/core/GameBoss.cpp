// GameBoss.cpp: the boss fight (akshat, boss rework). The boss itself (its moves, shots and hit
// boxes) is Boss in enemies/Enemy.cpp; this is the Game's side of it:
//   - the arena: when a boss appears the ground under the screen is levelled, with two ledges,
//     the camera holds still and the Forgetting Wall waits off-screen until the fight is over;
//   - ATTACK deflects its shots (a parry flash) and hits its weak point while it is stunned;
//     both players hitting the weak point within a moment is "Remember together" (bonus damage);
//   - contact with its body boxes, its shots hitting the players;
//   - the autoplay bots' fight (deflect, dodge the telegraphs, jump the train, hit the weak point).
#include "core/Config.h"
#include "core/Events.h"
#include "core/Game.h"
#include "core/Strings.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace {
const Color P_SPARK[2] = {{255, 180, 90, 255}, {120, 200, 255, 255}};   // as in Game.cpp
HitBox playerBox(const Player& pl) { return {pl.x, pl.y, pl.w, pl.h}; }
HitBox attackBox(const Player& pl) { return {pl.facing > 0 ? pl.x + pl.w : pl.x - 1.3f, pl.y, 1.3f, pl.h}; }
// a shot counts as parried a little outside the swing too: the window is about timing, not pixels
HitBox parryBox(const Player& pl) { HitBox a = attackBox(pl); return {a.x - 0.3f, a.y - 0.4f, a.w + 0.6f, a.h + 0.8f}; }
}  // namespace

void Game::startBossFight()
{
    // ---- the arena: the width of the screen, levelled to the ground most of it already stands on
    const float width = std::min(art::VIEW_TILES_H * cam.aspect, config().f("boss.arena_width", 19.0f));
    const int L = (int)std::floor(cam.x - width / 2 + 0.5f), R = L + (int)width;
    std::map<int, int> votes;
    for (int x = L; x < R; ++x) { int gt = level.groundTop(x); if (gt >= 0) votes[gt]++; }
    int F = LEVEL_H - 2, best = 0;
    for (auto& kv : votes) if (kv.second > best) { best = kv.second; F = kv.first; }
    (void)best;
    F = LEVEL_H - 2;   // always the bottom: the camera then frames the whole fight (votes kept for a log)
    // (six columns more each side: the wide 3D view shows past the arena)
    for (int x = std::max(level.firstCol(), L - 6); x <= std::min(level.endCol() - 1, R + 6); ++x)
        for (int y = 0; y < LEVEL_H; ++y) level.setTile(x, y, y >= F ? '#' : '.');
    // two ledges to get out of the way on (the train passes under them)
    // (not in full 3D: there the camera's angle turns them into big floating cubes)
    if (era < art::LAST_ERA)
        for (int k = 0; k < 3; ++k) { level.setTile(L + 3 + k, F - 3, '#'); level.setTile(R - 6 + k, F - 3, '#'); }
    arenaOn_ = true;
    arenaL_ = (float)L;
    arenaR_ = (float)R;
    arenaFloor_ = (float)F;
    for (auto& pl : players) {
        if (!pl.joined) continue;
        pl.x = std::max(arenaL_ + 0.5f, std::min(arenaR_ - pl.w - 0.5f, pl.x));
        if (pl.y + pl.h > F || boxHitsSolid(level, pl.x, pl.y, pl.w, pl.h, pl.index, solo())) { pl.y = F - pl.h - 0.01f; pl.vy = 0; }
    }
    enemies.erase(std::remove_if(enemies.begin(), enemies.end(), [&](const Enemy& e) {
        return e.cx() > L - 3 && e.cx() < R + 3;
    }), enemies.end());
    for (auto& f : frags) if (f.x > L - 1 && f.x < R + 1 && f.y > F - 0.3f) f.y = F - 0.6f;
    for (auto& pu : powerups) if (pu.x > L - 1 && pu.x < R + 1 && pu.y > F - 0.3f) pu.y = F - 0.7f;
    particles.burst(cam.x, (float)F, 60, Color{255, 255, 255, 200}, 12, 0.6f, 0.18f);

    // ---- the boss: its health from [boss] hp_<stage>, scaled by difficulty
    const FightTuning& ft = FIGHT_TUNING[std::max(0, std::min(DIFF_COUNT - 1, difficulty))];
    const int defHp = era == 2 ? 36 : (era == 4 ? 40 : 30);
    int hp = (int)std::lround(config().i("boss.hp_" + std::to_string(era), defHp) * ft.hpMul);
    boss.start(cam.x, tier(), hp, era, ft, (float)F, arenaL_, arenaR_);
    lastWeakHit_[0] = lastWeakHit_[1] = -99;
    bossStartT_ = time;
    bossWipes_ = 0;
    if (opt.autoplay) TraceLog(LOG_INFO, "EVENT t=%.1f boss arena %d..%d floor %d, hp %d (%s)", time, L, R, F, hp, difficultyId(difficulty));
}

void Game::keepPlayersInArena()
{
    if (!arenaOn_) return;
    if (!boss.active) { arenaOn_ = false; return; }
    for (auto& pl : players) {
        if (!pl.joined) continue;
        if (pl.x < arenaL_) { pl.x = arenaL_; pl.vx = std::max(0.0f, pl.vx); }
        if (pl.x + pl.w > arenaR_) { pl.x = arenaR_ - pl.w; pl.vx = std::min(0.0f, pl.vx); }
    }
}

void Game::bossRespawn()
{
    ++bossWipes_;
    boss.shots.clear();
    if (boss.kind == 4) { boss.state = BS_HOVER; boss.y = boss.groundY(); }
    else { boss.state = BS_RISE; boss.fromX = boss.x; boss.fromY = boss.y; boss.slamsLeft = 0; }
    boss.stateTime = 0;
    boss.shotTimer = 2.0f;
    if (!arenaOn_) return;
    // both back on the arena floor, on the side away from the boss
    float mid = (arenaL_ + arenaR_) / 2;
    float x0 = boss.x > mid ? arenaL_ + 3.0f : arenaR_ - 4.5f;
    for (int p = 0; p < 2; ++p)
        if (players[p].joined) players[p].spawnAt(x0 + 0.5f + p * 1.0f, arenaFloor_);
}

void Game::hitBossWeak(int p, int dmg, const char* how)
{
    if (!boss.vulnerable() || boss.hp <= 0 || boss.stunHits >= boss.ft.stunHits) return;   // a few hits per opening
    boss.hp = std::max(0, boss.hp - dmg);
    boss.hurt = 0.3f;
    boss.stunHits++;
    HitBox w = boss.weakBox();
    audio.play("boss_hit");
    cam.addShake(0.5f);
    hitPause = std::max(hitPause, 0.06f);
    particles.burst(w.cx(), w.cy(), 26, Color{255, 230, 140, 255}, 8, 0.5f, 0.16f);
    score += 50;
    if (opt.autoplay) TraceLog(LOG_INFO, "EVENT t=%.1f boss weak point hit by P%d (%s), hp %d", time, p + 1, how, boss.hp);
    if (p < 0) return;
    const int o = 1 - p;
    const float window = config().f("combat.together_window", 0.7f);
    if (!boss.coopDone && players[o].joined && time - lastWeakHit_[o] < window && boss.hp > 0) {
        // "Remember together": both of you, at once
        boss.coopDone = true;
        boss.hp = std::max(0, boss.hp - config().i("combat.together_bonus", 2));
        showBanner(T("boss_together"), T("boss_together_sub"), 1.6f);
        audio.play("unlock");
        audio.play("boss_hit", 1, 0.8f);
        cam.addShake(1.0f);
        hitPause = 0.14f;
        for (int q = 0; q < 2; ++q) particles.burst(w.cx(), w.cy(), 40, P_SPARK[q], 11, 0.8f, 0.2f);
        if (opt.autoplay) TraceLog(LOG_INFO, "EVENT t=%.1f boss REMEMBER TOGETHER, hp %d", time, boss.hp);
    }
    lastWeakHit_[p] = time;
}

void Game::bossPlayerCombat(float dt)
{
    for (int p = 0; p < 2; ++p) if (players[p].evAttack) swingUsed_[p] = false;
    if (!boss.active || boss.state == BS_DYING) return;
    for (int p = 0; p < 2; ++p) {
        Player& pl = players[p];
        if (!pl.alive()) continue;

        // ---- a ground pound next to the open weak point
        if (pl.evPoundLand && boss.vulnerable()) {
            HitBox w = boss.weakBox();
            if (std::fabs(w.cx() - pl.cx()) < 2.6f && pl.bottom() - (w.y + w.h) < 3.0f) hitBossWeak(p, 1, "pound");
        }

        // ---- the attack button: deflect shots, hit the weak point, or clank off its armour
        if (pl.attackTime > 0) {
            HitBox parry = parryBox(pl), atk = attackBox(pl);
            for (auto& s : boss.shots) {
                if (s.reflected || s.dead || !parry.overlaps(s.box())) continue;
                s.reflected = true;
                s.by = p;
                s.t = 0;
                boss.deflectT = 0.4f;
                boss.deflectX = s.x;
                boss.deflectY = s.y;
                audio.play("swipe");
                audio.play(boss.kind == 2 ? "dong" : "stomp", 0.8f, 1.4f);
                particles.burst(s.x, s.y, 18, WHITE, 7, 0.35f, 0.12f);
                particles.burst(s.x, s.y, 10, P_SPARK[p], 5, 0.4f, 0.12f);
                hitPause = std::max(hitPause, 0.05f);
                cam.addShake(0.3f);
                if (opt.autoplay) TraceLog(LOG_INFO, "EVENT t=%.1f boss shot deflected by P%d", time, p + 1);
            }
            if (!swingUsed_[p] && boss.state != BS_ENTER) {
                if (boss.vulnerable() && atk.overlaps(boss.weakBox())) {
                    swingUsed_[p] = true;
                    hitBossWeak(p, 1, "attack");
                } else {
                    HitBox b[3];
                    int n = boss.bodyBoxes(b);
                    for (int i = 0; i < n; ++i)
                        if (atk.overlaps(b[i])) {   // its armour: no damage outside an opening
                            swingUsed_[p] = true;
                            audio.play("hit", 0.6f, 0.6f);
                            particles.burst(atk.cx(), atk.cy(), 6, Color{200, 200, 210, 255}, 4, 0.25f, 0.08f);
                            break;
                        }
                }
            }
        }

        // ---- touching its body
        if (boss.state != BS_ENTER) {
            HitBox me = playerBox(pl), b[3];
            int n = boss.bodyBoxes(b);
            for (int i = 0; i < n; ++i) {
                if (!me.overlaps(b[i])) continue;
                float prevBottom = pl.bottom() - pl.vy * dt;
                bool onTop = pl.vy > 0 && prevBottom <= b[i].y + 0.5f;
                if (boss.stunned()) {
                    if (onTop) {   // a stomp on a stunned boss still counts (a little)
                        pl.vy = -ptune.stompBounce * 1.2f;
                        pl.pounding = false;
                        audio.play("stomp");
                        hitBossWeak(p, 1, "stomp");
                    }
                } else if (boss.dangerous()) {
                    if (pl.invuln <= 0 && pl.dashTime <= 0) {
                        hurtPlayer(p, false);
                        if (boss.state == BS_ATTACK) boss.hitSomeone = true;
                    }
                } else if (onTop) {
                    pl.vy = -ptune.stompBounce;   // you bounce off it: no damage outside an opening
                    audio.play("hit", 0.5f, 0.7f);
                } else {
                    float d = pl.cx() < b[i].cx() ? -1.0f : 1.0f;
                    pl.vx = d * 7.0f;
                    pl.x += d * 0.08f;
                    if (pl.onGround) pl.vy = -5;
                }
                break;
            }
        }

        // ---- its shots
        HitBox me = playerBox(pl);
        for (auto& s : boss.shots) {
            if (s.reflected || s.dead || !me.overlaps(s.box())) continue;
            if (pl.invuln > 0 || pl.dashTime > 0) continue;
            s.dead = true;
            particles.burst(s.x, s.y, 12, Color{230, 200, 255, 255}, 5, 0.4f, 0.12f);
            hurtPlayer(p, false);
            break;
        }
    }
}

bool Game::updateBossFight(float dt, float slowK)
{
    // nothing from outside wanders into the arena (the boss's own minions spawn inside it)
    if (arenaOn_)
        enemies.erase(std::remove_if(enemies.begin(), enemies.end(), [&](const Enemy& e) {
            return e.cx() < arenaL_ - 0.5f || e.cx() > arenaR_ + 0.5f;
        }), enemies.end());
    boss.held = bossHeldByPower();
    boss.update(dt, cam.left(), cam.right(), level, players, tier(), slowK);
    if (boss.wantsShotSound) audio.play(boss.kind == 2 ? "dong" : (boss.kind == 4 ? "dash" : "tick"), 0.55f, boss.kind == 2 ? 1.3f : 1.0f);
    if (boss.wantsWhistle) audio.play(boss.kind == 4 ? "encounter" : "slow", 0.8f);
    if (boss.wantsSlam) { audio.play("slam"); cam.addShake(0.8f); }
    if (boss.wantsStunSound) {
        audio.play("dong", 1.0f, 0.8f);
        if (opt.autoplay) TraceLog(LOG_INFO, "EVENT t=%.1f boss stunned (%.1fs), weak point open", time, boss.stunLen);
    }
    if (boss.wantsBlast) {
        // the Last Train recovers with a blast of steam: anyone at its front is pushed back down the
        // line (no harm), so its charge never starts point-blank
        const float dir = -(float)boss.side;   // the line runs from its end of the arena inwards
        for (auto& pl : players) {
            if (!pl.alive() || std::fabs(pl.cx() - boss.x) > 4.5f) continue;
            pl.vx = dir * 12.0f;
            pl.vy = -7.0f;
            pl.onGround = false;
            pl.wallLock = 0.3f;
        }
        audio.play("dash", 1.0f, 0.6f);
        particles.burst(boss.x + dir * 1.4f, boss.y, 40, Color{235, 235, 245, 220}, 9, 0.7f, 0.22f);
    }
    boss.wantsShotSound = boss.wantsWhistle = boss.wantsSlam = boss.wantsStunSound = boss.wantsBlast = false;

    if (boss.landed && boss.kind == 2) {
        // the school bell rings as it lands: anyone standing close is thrown back (no harm done)
        for (auto& pl : players) {
            if (!pl.alive() || !pl.onGround) continue;
            float dx = pl.cx() - boss.x;
            if (std::fabs(dx) > 3.0f) continue;
            pl.vx = (dx < 0 ? -1.0f : 1.0f) * 9.0f;
            pl.vy = -8.0f;
            pl.onGround = false;
            pl.wallLock = 0.2f;
        }
        particles.burst(boss.x, boss.floorY - 0.1f, 40, Color{255, 220, 120, 255}, 9, 0.6f, 0.2f);
    }
    if (boss.landed && boss.kind != 2)
        particles.burst(boss.x, boss.floorY - 0.1f, 30, Color{220, 210, 190, 255}, 8, 0.5f, 0.18f, 15);

    // ---- deflected shots reaching the boss
    for (auto& s : boss.shots) {
        if (!s.reflected || s.dead) continue;
        HitBox b[3];
        int n = boss.bodyBoxes(b);
        bool hit = s.box().overlaps(boss.weakBox());
        for (int i = 0; i < n && !hit; ++i) hit = s.box().overlaps(b[i]);
        if (!hit) continue;
        s.dead = true;
        particles.burst(s.x, s.y, 24, Color{255, 240, 200, 255}, 8, 0.5f, 0.15f);
        if (boss.stunned()) hitBossWeak(s.by, 1, "deflect");
        else if (boss.state == BS_HOVER || boss.state == BS_AIM || boss.state == BS_RISE || boss.state == BS_RECOVER) {
            boss.stun(boss.ft.stunDeflect);   // its own shot knocks it dizzy: the weak point opens
            cam.addShake(0.7f);
            hitPause = std::max(hitPause, 0.08f);
            if (opt.autoplay) TraceLog(LOG_INFO, "EVENT t=%.1f boss knocked dizzy by its own shot (P%d)", time, s.by + 1);
        } else audio.play("hit", 0.6f, 0.6f);
    }

    if (boss.phase == 2 && !boss.phase2Told && boss.hp > 0) {
        boss.phase2Told = true;
        showBanner(T("boss_phase2_" + std::to_string(boss.kind)), T("boss_phase2_sub"), 1.8f);
        cam.addShake(0.8f);
        if (opt.autoplay) TraceLog(LOG_INFO, "EVENT t=%.1f boss phase 2", time);
    }

    if (boss.wantsSpawn) {
        int type = tier() >= 2 && (std::rand() % 3 == 0) ? EN_BRUTE : (std::rand() % 2 ? EN_FLYER : EN_BLOB);
        if (boss.spawnsFlyers()) type = EN_FLYER;
        Enemy e = makeEnemy(type, boss.x, boss.y, tier(), etune);
        e.y = std::min(boss.y, boss.floorY - e.h - 0.05f);
        e.baseY = e.y;
        if (!solo() && era >= 1 && std::rand() % 3 == 0) e.owner = std::rand() % 2;
        enemies.push_back(e);
        particles.burst(boss.x, boss.y, 16, Color{160, 100, 220, 255}, 5, 0.5f, 0.15f);
    }
    if (boss.hp <= 0 && boss.state != BS_DYING) {
        boss.state = BS_DYING;
        boss.stateTime = 0;
        boss.shots.clear();
        audio.play("boss_hit", 1, 0.6f);
        cam.addShake(1.2f);
        spawnFragments(boss.x, boss.y, 12, 1.5f);
        particles.burst(boss.x, boss.y, 80, Color{220, 180, 255, 255}, 12, 1.4f, 0.25f);
        score += 500;
        if (opt.autoplay) TraceLog(LOG_INFO, "EVENT t=%.1f boss fight over in %.1fs with %d wipes", time, time - bossStartT_, bossWipes_);
        events().bossDefeated.emit();
        audio.setBoss(false);
    }
    if (boss.defeated) {
        boss.active = false;
        arenaOn_ = false;
        bossBeatenThisEra = true;
        // akshat: a few words with the beaten boss, then the memory comes back
        cutscene.play(*this, "boss_" + Cutscene::bossId(boss.kind) + "_defeat");
        cutscene.then([this] { memoryRestored(); });
        return true;
    }
    return false;
}

// ------------------------------------------------------------------ the bots' boss fight
bool Game::botBoss(int p, PlayerInput& in, int& hold)
{
    const Player& pl = players[p];
    if (!boss.active || boss.state == BS_DYING || !arenaOn_) return false;
    PlayerInput b;
    b.abilityPressed = in.abilityPressed && in.down;   // Sam's time slow
    b.down = b.abilityPressed;
    const Player& other = players[1 - p];
    const float me = pl.cx();
    auto go = [&](float tx, float tol) { if (tx > me + tol) b.right = true; else if (tx < me - tol) b.left = true; };
    auto faceTo = [&](float tx) { if (tx > me && pl.facing < 0) b.right = true; if (tx < me && pl.facing > 0) b.left = true; };
    bool busy = false;

    // a Faded partner first
    if (other.joined && other.faded) { go(other.cx(), 0.2f); busy = true; }

    // deflect a shot that is about to reach me. Like a person, a bot only gets about half of them:
    // the rest it jumps over (rings along the floor, steam) or backs away from.
    const int skill = config().i("combat.bot_deflect_percent", 50);
    for (const auto& s : boss.shots) {
        if (busy) break;
        if (s.reflected) continue;
        float dx = s.x - me, dy = s.y - pl.cy();
        bool coming = (dx > 0 && s.vx < 1.0f) || (dx < 0 && s.vx > -1.0f);
        if (!coming || std::fabs(dx) > 2.6f || std::fabs(dy) > 1.8f) continue;
        const bool overhead = dy < -1.0f && std::fabs(dx) < 1.3f;   // a swing cannot reach it: step aside
        if (overhead) {
            go(dx > 0 ? me - 2.5f : me + 2.5f, 0.1f);
        } else if ((unsigned)(s.id * 37 + p * 11) % 100 < (unsigned)skill) {
            faceTo(s.x);
            if (std::fabs(dx) < 2.0f && pl.attackCooldown <= 0 && (pl.facing > 0) == (dx > 0)) b.attackPressed = true;
        } else if (s.type == SHOT_RING_GROUND || s.type == SHOT_STEAM) {
            if (std::fabs(dx) < 1.7f && pl.onGround) { b.jumpPressed = true; hold = 18; }
        } else {
            go(s.x > me ? me - 3 : me + 3, 0.1f);
        }
        busy = true;
    }

    // the weak point is open: go and hit it
    if (!busy && boss.stunned()) {
        HitBox w = boss.weakBox();
        float stand = me < w.cx() ? w.x - 0.7f : w.x + w.w + 0.7f;
        stand = std::max(arenaL_ + 0.5f, std::min(arenaR_ - 0.5f, stand));
        go(stand, 0.25f);
        if (std::fabs(me - stand) < 1.0f) {
            faceTo(w.cx());
            if (attackBox(pl).overlaps(w)) { if (pl.attackCooldown <= 0) b.attackPressed = true; }
            else if (w.y + w.h < pl.y + 0.3f && pl.onGround) { b.jumpPressed = true; hold = 22; }   // too high: a jump attack
        }
        busy = true;
    }

    // its big attack: get out of the marker, or jump the train
    if (!busy && boss.aiming()) {
        if (boss.kind == 4) {
            if (boss.state == BS_AIM) {
                // the whistle: get a few tiles of room in front of it to jump it
                float dir = boss.toX > boss.x ? 1.0f : -1.0f;
                float gap = (me - (boss.x + dir * 1.32f)) * dir;
                if (gap < 3.5f) { if (dir > 0) b.right = true; else b.left = true; }
            }
            if (boss.state == BS_ATTACK) {
                float dir = boss.toX > boss.fromX ? 1.0f : -1.0f;
                float gap = (me - (boss.x + dir * 1.32f)) * dir - pl.w / 2;
                if (gap > -0.3f && gap < boss.ft.trainSpeed * 0.17f + 0.3f && pl.onGround) { b.jumpPressed = true; hold = 30; }
                if (!pl.onGround && pl.vy > 0.5f && pl.airJumps == 0 && pl.has[AB_DOUBLE_JUMP] && std::fabs(boss.x - me) < 2.8f) { b.jumpPressed = true; hold = 20; }
            }
        } else if (std::fabs(boss.toX - me) < 3.6f) {
            bool roomRight = arenaR_ - boss.toX > boss.toX - arenaL_;
            bool goRight = me > boss.toX ? (arenaR_ - me > 1.5f) : !roomRight ? false : (me - arenaL_ < 1.5f);
            if (goRight) b.right = true; else b.left = true;
        }
        busy = true;
    }

    // otherwise keep a spot a few tiles to one side of it
    if (!busy) {
        float mid = (arenaL_ + arenaR_) / 2;
        float spot = boss.kind == 4 ? mid + (p ? 2.0f : -2.0f) : boss.x + (p ? 4.0f : -4.0f);
        if (spot < arenaL_ + 1.5f || spot > arenaR_ - 1.5f) spot = boss.x + (p ? -4.0f : 4.0f);
        go(std::max(arenaL_ + 1.0f, std::min(arenaR_ - 1.0f, spot)), 0.6f);
    }
    if (hold > 0) { b.jumpHeld = true; --hold; }
    in = b;
    return true;
}
