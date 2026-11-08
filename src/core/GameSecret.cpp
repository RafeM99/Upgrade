// GameSecret.cpp: the extras of the secret level "era 10" only (Rafale's UI thread).
//   Gojo pull:        the Forgetting drags everyone in towards it, and swoops when it lets go
//   telegraphed slice: red lines through the Forgetting that flash white and cut a moment later (Retinazer style)
//   Sam throw:         Gran (key F, pad Y) hurls a 3D Sam at the boss
#include "core/Game.h"

#include <algorithm>
#include <cmath>

namespace {
float segDist(float px, float py, float ax, float ay, float dx, float dy)   // distance to the infinite line
{
    float len = std::hypot(dx, dy);
    if (len < 1e-4f) return std::hypot(px - ax, py - ay);
    return std::fabs((px - ax) * dy - (py - ay) * dx) / len;
}
}  // namespace

void Game::secretPull()
{
    secretPullT = 1.8f;
    cam.addShake(0.4f);
    audio.play("slow");
}

void Game::secretSlices()
{
    // three lines, one after the other, each aimed at where Gran stands when it is drawn
    for (int i = 0; i < 3; ++i) {
        const Player& tgt = players[(solo() || players[0].alive()) ? 0 : 1];
        SecretSlice s;
        s.x = boss.x; s.y = boss.y;
        float ang = std::atan2(tgt.cy() - boss.y, tgt.cx() - boss.x) + (i == 1 ? 0.55f : (i == 2 ? -0.55f : 0.0f));
        s.dx = std::cos(ang); s.dy = std::sin(ang);
        s.t = -0.55f * i;   // staggered
        slices.push_back(s);
    }
}

void Game::secretThrow(int who)
{
    Player& pl = players[who];
    SamThrow t;
    t.x = pl.cx() + pl.facing * 0.6f; t.y = pl.cy() - 0.3f;
    t.vx = pl.facing * 17.0f + pl.vx * 0.3f; t.vy = -3.5f;
    thrown.push_back(t);
    throwCooldown = 0.55f;
    audio.play("attack");
    particles.burst(t.x, t.y, 10, Color{120, 180, 255, 255}, 4, 0.4f, 0.12f);
}

void Game::updateSecret(float dt)
{
    if (!secret) return;
    throwCooldown = std::max(0.0f, throwCooldown - dt);

    // ---- Gran throws Sam
    {
        Player& g0 = players[0];
        bool key = IsKeyPressed(KEY_F);
        int pad = input.padFor(0);
        if (pad >= 0) key |= IsGamepadButtonPressed(pad, GAMEPAD_BUTTON_RIGHT_FACE_UP);
        if (key && throwCooldown <= 0 && g0.alive() && !opt.autoplay) secretThrow(0);
        if (opt.autoplay && throwCooldown <= 0 && g0.alive() && boss.active && ((int)(time * 2)) % 3 == 0) secretThrow(0);   // the bot throws too, for tests
    }
    for (auto& t : thrown) {
        t.t += dt;
        t.vy += 14.0f * dt;
        t.x += t.vx * dt;
        t.y += t.vy * dt;
        t.rot += 11.0f * dt * (t.vx < 0 ? -1.0f : 1.0f);
        if (level.isSolidChar(level.tile((int)std::floor(t.x), (int)std::floor(t.y)))) {
            t.dead = true;
            particles.burst(t.x, t.y, 12, Color{120, 180, 255, 255}, 4, 0.4f, 0.12f);
        }
        if (boss.active && boss.state != 5 && boss.hurt <= 0 &&
            std::fabs(t.x - boss.x) < boss.w / 2 + 0.3f && std::fabs(t.y - boss.y) < boss.h / 2 + 0.3f) {
            boss.hp -= 1;
            boss.hurt = 0.35f;
            t.dead = true;
            audio.play("boss_hit");
            cam.addShake(0.35f);
            score += 40;
            particles.burst(t.x, t.y, 24, Color{200, 150, 255, 255}, 6, 0.5f, 0.14f);
        }
        for (auto& e : enemies) {
            if (e.hp <= 0 || t.dead) continue;
            if (std::fabs(t.x - e.cx()) < 0.9f && std::fabs(t.y - e.cy()) < 0.9f) { damageEnemy(e, 2, 0, 6.0f); t.dead = true; }
        }
        if (t.t > 2.5f) t.dead = true;
    }
    thrown.erase(std::remove_if(thrown.begin(), thrown.end(), [](const SamThrow& t) { return t.dead; }), thrown.end());

    if (!boss.active || boss.state == 5) { slices.clear(); secretPullT = 0; return; }

    // ---- the Gojo pull
    if (secretPullT > 0) {
        secretPullT -= dt;
        if (boss.state == 1) boss.stateTime = 0;   // it holds still while it pulls
        for (int p = 0; p < 2; ++p) {
            Player& pl = players[p];
            if (!pl.joined || !pl.alive()) continue;
            float dx = boss.x - pl.cx(), dy = boss.y - pl.cy(), d = std::hypot(dx, dy);
            if (d < 1.6f) {
                if (pl.invuln <= 0 && pl.dashTime <= 0) hurtPlayer(p, false);
                continue;
            }
            float step = std::min(d - 1.5f, 9.5f * dt);
            float nx = pl.x + dx / d * step, ny = pl.y + dy / d * step;
            if (!level.isSolidChar(level.tile((int)std::floor(nx + pl.w / 2), (int)std::floor(ny + pl.h / 2)))) { pl.x = nx; pl.y = ny; }
            pl.vy *= 0.8f;
            if (((int)(time * 30)) % 3 == 0) particles.burst(pl.cx(), pl.cy(), 1, Color{200, 150, 255, 255}, 2, 0.4f, 0.1f);
        }
        if (secretPullT <= 0 && boss.state == 1) boss.stateTime = 99.0f;   // lets go and swoops
    }

    // ---- the telegraphed slices: 0.9 s red line, then 0.2 s of white cut
    const float warn = 0.9f, fire = 0.2f;
    for (auto& s : slices) {
        float before = s.t;
        s.t += dt;
        if (before < warn && s.t >= warn) { audio.play("pound"); cam.addShake(0.4f); }
        if (s.t >= warn && s.t < warn + fire)
            for (int p = 0; p < 2; ++p) {
                Player& pl = players[p];
                if (!pl.joined || !pl.alive() || pl.invuln > 0 || pl.dashTime > 0) continue;
                if (segDist(pl.cx(), pl.cy(), s.x, s.y, s.dx, s.dy) < 0.6f) hurtPlayer(p, false);
            }
    }
    slices.erase(std::remove_if(slices.begin(), slices.end(), [&](const SecretSlice& s) { return s.t > warn + fire; }), slices.end());
}
