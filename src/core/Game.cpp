// Owner: Systems (Camilla's Claude). Shared hotspot: keep edits small and say so in your status file.
#include "core/Game.h"
#include <algorithm>
#include "core/Config.h"
#include "core/Events.h"
#include "core/Paths.h"
#include "core/Strings.h"
#include "raylib.h"

namespace {
// Abilities gained at each era (brief section 7). [era][player]
const char* kAbilities[4][2] = {
    {"", ""},
    {"double_jump", "dash"},
    {"ground_pound", "wall_jump"},
    {"glide", "time_slow"},
};
}

void Game::init() {
    Strings::load(Paths::asset("strings.txt"));
    level.loadPool(Paths::asset("chunks"));
    spawner.reset(level);
    era.init();
    bar.init();
    wall.init();
    enemies.clear();
    camera.init();
    renderer.init();
    hud.init();
    audio.init();

    for (int i = 0; i < 2; ++i) players[i].index = i;
    players[0].spawn({2.0f, level.spawnHeight(2)});
    players[0].active = true;

    Events& ev = events();
    ev.fragmentCollected.connect([this](int, int amount) {
        if (complete) return;
        bar.add(amount * Config::getFloat("memory", "fragment_value", 1.0f));
        era.checkBar(bar.value);
    });
    ev.memoryLost.connect([this]() { bar.loseMemory(era.thresholdFor(era.current())); });
    ev.eraChanged.connect([this](int e) { onEraChanged(e); });
    ev.abilityUnlocked.connect([this](int p, const std::string& name) { players[p].abilities.insert(name); });
    ev.memoryComplete.connect([this]() {
        complete = true;
        popupTitle = Strings::get("memory_restored") + ": " + Strings::get("memory4");
    });
}

void Game::setAutoplay(bool on) {
    autoplay = on;
    input.autoplay = on;
}

void Game::onEraChanged(int e) {
    if (e < 1 || e > 3) return;
    freezeTimer = Config::getFloat("memory", "transition_seconds", 1.5f);
    popupTitle = Strings::get("memory_restored") + ": " + Strings::get("memory" + std::to_string(e));
    popupLine.clear();
    for (int p = 0; p < 2; ++p) {
        std::string ability = kAbilities[e][p];
        events().abilityUnlocked.emit(p, ability);
        if (!popupLine.empty()) popupLine += "      ";
        popupLine += "P" + std::to_string(p + 1) + " " + Strings::get("new_ability") + ": " +
                     Strings::get("ability_" + ability);
    }
    spawner.forceAbility(kAbilities[e][0]);
}

std::set<std::string> Game::unlockedAbilities() const {
    std::set<std::string> all = players[0].abilities;
    all.insert(players[1].abilities.begin(), players[1].abilities.end());
    return all;
}

void Game::frameInput() {
    input.poll();
    if (IsKeyPressed(KEY_F1)) events().fragmentCollected.emit(0, 5);   // debug: fill the bar
    ++frames;
    if (autoFill && frames % 10 == 0) events().fragmentCollected.emit(0, 2);
    if (autoplay && frames % 120 == 0)
        TraceLog(LOG_INFO, "UPGRADE: frame %ld P1 x=%.1f y=%.1f P2 x=%.1f y=%.1f bar=%.0f era=%d chunks=%d..%d",
                 frames, players[0].pos.x, players[0].pos.y, players[1].pos.x, players[1].pos.y, bar.value,
                 era.current(), level.beginX(), level.endX());
}

void Game::update(float dt) {
    InputState in[2] = {input.take(0), input.take(1)};

    // P2 joins next to P1 by pressing any of their buttons.
    if (!players[1].active && in[1].anyPressed) {
        Vector2 at = players[0].pos;
        players[1].spawn({at.x - 1.0f, at.y + 1.0f});
        players[1].active = true;
    }

    if (freezeTimer > 0.0f) {
        freezeTimer -= dt;
        return;
    }
    if (complete) return;

    for (Player& p : players)
        if (p.active) p.update(dt, in[p.index], level);

    camera.update(dt, players, 2);
    spawner.update(level, camera.rightEdge(), era.current(), unlockedAbilities());

    float minPlayerX = 1e9f;
    for (const Player& p : players)
        if (p.active) minPlayerX = std::min(minPlayerX, p.pos.x);
    wall.update(dt, era.current(), minPlayerX);

    float behind = Config::getFloat("spawner", "behind_tiles", 40.0f);
    level.cullBefore(std::min(camera.leftEdge(), wall.enabled ? wall.x : 1e9f) - behind);
    enemies.update(dt, *this);
    audio.update();
}

void Game::draw() {
    renderer.draw(*this);
    hud.draw(*this);
}

void Game::shutdown() {
    audio.shutdown();
    renderer.shutdown();
}
