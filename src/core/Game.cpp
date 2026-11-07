#include "core/Game.h"
#include <algorithm>
#include <cmath>
#include "core/Config.h"
#include "core/Events.h"
#include "core/Paths.h"
#include "core/Strings.h"
#include "raylib.h"

void Game::init() {
    Strings::load(Paths::asset("strings.txt"));
    level.loadPool(Paths::asset("chunks"));
    renderer.init();
    hud.init();
    audio.init();
    events().eraChanged.connect([this](int e) { onEraChanged(e); });
    restart();
    screen = Screen::Title;
}

void Game::restart() {
    spawner.reset(level);
    enemies.clear();
    era.init();
    camera.init();
    for (int i = 0; i < 2; ++i) {
        players[i] = Player();
        players[i].index = i;
        players[i].god = god;
    }
    players[0].spawn({2.0f, level.spawnHeight(2)});
    players[0].invuln = 0.0f;
    players[0].active = true;

    coins = score = deaths = 0;
    playTime = freezeTimer = deathTimer = 0.0f;
    world = 0;
    worldStartX = 2.0f;
    popupTitle.clear();
    popupLine.clear();
    popupTroll.clear();
    screen = Screen::Playing;
}

void Game::setAutoplay(bool on) {
    autoplay = on;
    input.autoplay = on;
    if (on) screen = Screen::Playing;
}

void Game::setGod(bool on) {
    god = on;
    for (Player& p : players) p.god = on;
}

std::string Game::randomLine(const std::string& prefix) const {
    int n = 0;
    while (n < 100 && Strings::get(prefix + std::to_string(n)) != prefix + std::to_string(n)) ++n;
    if (n == 0) return "";
    return Strings::get(prefix + std::to_string(GetRandomValue(0, n - 1)));
}

void Game::onEraChanged(int e) {
    if (e < 1) return;
    freezeTimer = Config::getFloat("era", "transition_seconds", 2.5f);
    popupTitle = Strings::get("world_clear");
    popupLine = Strings::get("welcome_to") + " " + Strings::get("era" + std::to_string(e) + "_name");
    popupTroll = Strings::get("era" + std::to_string(e) + "_troll");
}

void Game::clearWorld() {
    ++world;
    const std::vector<float>& f = spawner.flagXs();
    worldStartX = (int)f.size() >= world ? f[world - 1] : leadX();
    events().worldCleared.emit();
    if (!era.isLast()) {
        era.setEra(era.current() + 1);   // onEraChanged shows the upgrade popup
        return;
    }
    // Past the Wii era the worlds never stop. They just get meaner.
    if (world == kEraCount) events().gameComplete.emit();
    freezeTimer = Config::getFloat("era", "transition_seconds", 2.5f);
    popupTitle = Strings::get(world == kEraCount ? "all_eras_clear" : "world_clear");
    popupLine = Strings::get("endless_world") + " " + std::to_string(world + 1);
    popupTroll = world == kEraCount ? Strings::get("endless_intro") : randomLine("endless_troll_");
}

void Game::skipToFlag() {
    spawner.ensureFlag(level, world, players[1].active);
    float fx = nextFlagX();
    if (fx < 0.0f) return;
    for (Player& p : players) {
        if (!p.active) continue;
        float x = fx - 3.0f - p.index * 0.9f;
        p.spawn({x, level.spawnHeight((int)x)});
        p.invuln = 0.0f;
    }
}

void Game::reachFlag(float flagX, float heightOnPole) {
    // Higher on the pole is worth more, like you remember.
    TraceLog(LOG_INFO, "UPGRADE: world %d flag reached at x=%.1f (height %.1f)", world + 1, flagX, heightOnPole);
    int bonus = heightOnPole > 6.0f ? 5000 : heightOnPole > 4.0f ? 2000 : heightOnPole > 2.0f ? 800 : 400;
    addScore(bonus, {flagX, 10.0f});
    clearWorld();
}

void Game::addCoin(Vector2 at, bool fromBlock) {
    ++coins;
    score += 100;
    if (fromBlock) enemies.coinPop(at);
    events().coinCollected.emit();
}

void Game::addScore(int points, Vector2 at) {
    score += points;
    enemies.popText(at, std::to_string(points));
}

float Game::nextFlagX() const {
    const std::vector<float>& f = spawner.flagXs();
    return world < (int)f.size() ? f[world] : -1.0f;
}

float Game::leadX() const {
    float x = players[0].pos.x;
    if (players[1].active) x = std::max(x, players[1].pos.x);
    return x;
}

void Game::frameInput() {
    input.poll();
    ++frames;
    if (screen == Screen::Playing && freezeTimer <= 0.0f) {
        if (!autoplay && IsKeyPressed(KEY_F1)) skipToFlag();   // debug: jump to this world's flag
        if (autoAdvance && frames % 360 == 0) skipToFlag();
    }
    if (autoplay && frames % 120 == 0) {
        std::string chunk = "?";
        for (const PlacedChunk& pc : level.placedChunks())
            if (players[0].pos.x >= pc.x0 && players[0].pos.x < pc.x0 + level.pool()[pc.poolIndex].width)
                chunk = level.pool()[pc.poolIndex].name;
        TraceLog(LOG_INFO,
                 "UPGRADE: frame %ld P1 x=%.1f y=%.1f vy=%.1f in %s, P2 x=%.1f y=%.1f, world=%d era=%d deaths=%d coins=%d score=%d",
                 frames, players[0].pos.x, players[0].pos.y, players[0].vel.y, chunk.c_str(), players[1].pos.x,
                 players[1].pos.y, world + 1, era.current(), deaths, coins, score);
    }
}

void Game::update(float dt) {
    InputState in[2] = {input.take(0), input.take(1)};
    if (titleNagTimer > 0.0f) titleNagTimer -= dt;
    if (deathTimer > 0.0f) deathTimer -= dt;

    switch (screen) {
        case Screen::Title:
            camera.update(dt, players, 2, era.current());
            if (in[0].jumpPressed || in[1].jumpPressed) {
                restart();
            } else if (in[0].anyPressed || in[1].anyPressed) {
                titleNag = randomLine("title_nag_");   // any key except the right one
                titleNagTimer = 2.0f;
            }
            return;
        case Screen::Playing:
            updatePlaying(dt, in);
            return;
    }
}

void Game::updatePlaying(float dt, const InputState in[2]) {
    // P2 joins next to P1 by pressing any of their buttons.
    if (!players[1].active && in[1].anyPressed) {
        Vector2 at = players[0].dead ? players[0].safePos() : players[0].pos;
        players[1].spawn({at.x - 1.0f, at.y + 1.0f});
        players[1].active = true;
    }

    if (freezeTimer > 0.0f) {
        freezeTimer -= dt;
        camera.update(dt, players, 2, era.current());
        return;
    }
    playTime += dt;

    for (Player& p : players)
        if (p.active) p.update(dt, in[p.index], level);

    for (Player& p : players) {
        if (!p.active || p.dead) continue;
        if (p.bumped) handleBump(p);

        // Hidden spikes pop up when you are nearly on top of them.
        Vector2 c = p.centre();
        for (int tx = (int)std::floor(c.x) - 2; tx <= (int)std::floor(c.x) + 2; ++tx)
            for (int ty = (int)std::floor(p.pos.y) - 1; ty <= (int)std::floor(p.pos.y) + 1; ++ty)
                if (level.tileAt(tx, ty) == 'V' && std::fabs(tx + 0.5f - c.x) < 1.3f) level.setTile(tx, ty, '^');
        if (level.touchesSpike(p.box())) p.kill();
    }

    level.update(dt);
    enemies.update(dt, *this);

    for (Player& p : players) {
        if (p.justDied) {
            p.justDied = false;
            ++deaths;
            deathLine = randomLine("death_");
            deathTimer = 2.5f;
            events().playerDied.emit(p.index);
        }
        if (p.active && p.dead && p.deadTimer <= 0.0f) respawn(p);
    }

    // Co-op: a player left off-screen for a moment is pulled back to their partner.
    for (Player& p : players) {
        Player& other = players[1 - p.index];
        float& t = offscreen[p.index];
        bool away = p.pos.x < camera.leftEdge() - 1.0f || p.pos.x > camera.rightEdge() + 1.0f;
        t = (p.active && !p.dead && other.active && !other.dead && away) ? t + dt : 0.0f;
        if (t > Config::getFloat("player", "offscreen_pull_seconds", 1.5f)) {
            p.spawn({other.safePos().x, other.safePos().y + 0.3f});
            t = 0.0f;
        }
    }

    camera.update(dt, players, 2, era.current());
    spawner.update(level, camera.rightEdge(), players[1].active);
    float behind = Config::getFloat("spawner", "behind_tiles", 40.0f);
    level.cullBefore(camera.leftEdge() - behind);
    audio.update();
}

void Game::handleBump(Player& p) {
    int bx = p.bumpX, by = p.bumpY;
    char t = level.tileAt(bx, by);
    Vector2 above = {bx + 0.5f, by + 1.2f};
    switch (t) {
        case '?':
        case 'H':
            enemies.bumpKill(bx, by + 1, *this);
            level.setTile(bx, by, 'U');
            addCoin(above, true);
            break;
        case '!':
            enemies.bumpKill(bx, by + 1, *this);
            level.setTile(bx, by, 'U');
            enemies.spawn('S', {bx + 0.5f, by + 1.0f}, true);
            enemies.popText({above.x, above.y + 1.0f}, Strings::get("troll_block"));
            break;
        case '%':
            enemies.bumpKill(bx, by + 1, *this);
            level.setTile(bx, by, '.');
            level.shatter(bx, by, '%');
            addScore(50, above);
            return;
        default:
            break;
    }
    level.bump(bx, by);
}

void Game::respawn(Player& p) {
    Player& other = players[1 - p.index];
    Vector2 at;
    if (other.active && !other.dead) {
        at = {other.safePos().x, other.safePos().y + 0.3f};
    } else {
        // Back to the start of the chunk you died in. Rage responsibly.
        int x0 = std::max(level.chunkStartAt(p.deathPos.x), level.beginX());
        at = {x0 + 1.0f, level.spawnHeight(x0 + 1)};
    }
    p.spawn(at);

    std::vector<Rectangle> boxes;
    for (const Player& q : players)
        if (q.active && !q.dead) boxes.push_back(q.box());
    level.resetTraps(boxes);
}

void Game::draw() {
    renderer.draw(*this);
    hud.draw(*this);
}

void Game::shutdown() {
    audio.shutdown();
    renderer.shutdown();
}
