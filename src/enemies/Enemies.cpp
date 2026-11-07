// Owner: Systems (Camilla's Claude).
#include "enemies/Enemies.h"
#include <algorithm>
#include <cmath>
#include "core/Config.h"
#include "core/Events.h"
#include "core/Game.h"

void EnemyManager::clear() {
    enemies.clear();
    fragments.clear();
}

void EnemyManager::update(float dt, Game& game) {
    // Turn new chunk markers into enemies and fragments.
    for (const SpawnMarker& m : game.level.newMarkers) {
        Vector2 centre = {m.x + 0.5f, m.y + 0.5f};
        if (m.type == 'F') {
            fragments.push_back({centre, false});
        } else if (m.type == 'B' && game.era.current() < 1) {
            continue;   // Fog Brute appears from the 8-bit era onward
        } else {
            int hp = m.type == 'B' ? Config::getInt("enemies", "brute_hp", 3) : 1;
            enemies.push_back({m.type, centre, centre, hp, true});
        }
    }
    game.level.newMarkers.clear();

    // Fragments drift toward the nearest player and fill the bar on touch.
    const float pullRange = Config::getFloat("enemies", "fragment_pull_range", 3.0f);
    const float pullSpeed = Config::getFloat("enemies", "fragment_pull_speed", 8.0f);
    for (Fragment& f : fragments) {
        if (f.taken) continue;
        int nearest = -1;
        float best = 1e9f;
        for (int p = 0; p < 2; ++p) {
            const Player& pl = game.players[p];
            if (!pl.active || pl.faded) continue;
            Vector2 c = pl.centre();
            float d = std::hypot(c.x - f.pos.x, c.y - f.pos.y);
            if (d < best) { best = d; nearest = p; }
        }
        if (nearest < 0) continue;
        if (best < 0.7f) {
            f.taken = true;
            events().fragmentCollected.emit(nearest, 1);
        } else if (best < pullRange) {
            Vector2 c = game.players[nearest].centre();
            f.pos.x += (c.x - f.pos.x) / best * pullSpeed * dt;
            f.pos.y += (c.y - f.pos.y) / best * pullSpeed * dt;
        }
    }

    // Forget anything left behind the level's first chunk.
    float minX = (float)game.level.beginX();
    fragments.erase(std::remove_if(fragments.begin(), fragments.end(),
                                   [minX](const Fragment& f) { return f.taken || f.pos.x < minX; }),
                    fragments.end());
    enemies.erase(std::remove_if(enemies.begin(), enemies.end(),
                                 [minX](const Enemy& e) { return !e.alive || e.pos.x < minX; }),
                  enemies.end());
}
