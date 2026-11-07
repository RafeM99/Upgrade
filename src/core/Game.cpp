#include "core/Game.h"
#include "core/Config.h"
#include "core/Events.h"
#include "core/Paths.h"
#include "core/Strings.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
constexpr float DT = 1.0f / 60.0f;
const Color P_SPARK[2] = {{255, 180, 90, 255}, {120, 200, 255, 255}};
Color fragColor(int era) { return era <= 1 ? Color{235, 235, 225, 255} : Color{255, 220, 90, 255}; }

bool overlap(float ax, float ay, float aw, float ah, float bx, float by, float bw, float bh)
{
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}
}  // namespace

// ------------------------------------------------------------------ setup
void Game::init(const Options& o)
{
    opt = o;
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT);
    InitWindow(opt.width, opt.height, "Upgrade");
    SetExitKey(KEY_NULL);   // Esc opens the pause menu instead of quitting
    SetTargetFPS(60);

    config().load(assetPath("config.ini"));
    strings().load(assetPath("strings.txt"));
    input.load();
    ptune.load();
    etune.load();
    art.load();
    renderer.init();
    audio.init();

    std::string log;
    int n = loadChunks(assetPath("chunks"), chunks, log);
    TraceLog(LOG_INFO, "UPGRADE: loaded %d chunks", n);
    if (!log.empty()) TraceLog(LOG_WARNING, "UPGRADE chunk problems:\n%s", log.c_str());
    unsigned seed = opt.seed ? opt.seed : (unsigned)(GetTime() * 1000) ^ 0x5eed;
    rng_.seed(seed);
    spawner.init(chunks, seed);
    spawner.generatedChance = config().f("level.generated_chunk_chance", 0.3f);
    spawner.stageShare = config().f("level.stage_chunk_share", 0.6f);
    fillSpeed_ = std::max(0.1f, config().f("memory.fill_speed", 1.0f));

    players[0].index = 0;
    players[1].index = 1;
    players[0].joined = true;
    if (opt.twoPlayers || opt.autoplay) players[1].joined = opt.twoPlayers;

    // sounds react to events, so the systems never call audio directly for these
    events().playerFaded.listen([this](int) { audio.play("fade"); });
    events().playerRevived.listen([this](int) { audio.play("revive"); });

    if (opt.autoplay) {   // a log of what happens, for headless test runs
        events().eraChanged.listen([this](int e) { TraceLog(LOG_INFO, "EVENT t=%.1f era -> %d", time, e); });
        events().bossAppeared.listen([this]() { TraceLog(LOG_INFO, "EVENT t=%.1f boss appears", time); });
        events().bossDefeated.listen([this]() { TraceLog(LOG_INFO, "EVENT t=%.1f boss defeated", time); });
        events().memoryLost.listen([this]() { TraceLog(LOG_INFO, "EVENT t=%.1f memory lost, bar %.0f", time, bar); });
        events().memoryComplete.listen([this]() { TraceLog(LOG_INFO, "EVENT t=%.1f memory complete, score %d", time, score); });
        events().playerFaded.listen([this](int p) { TraceLog(LOG_INFO, "EVENT t=%.1f P%d faded", time, p + 1); });
        events().playerRevived.listen([this](int p) { TraceLog(LOG_INFO, "EVENT t=%.1f P%d revived", time, p + 1); });
        events().powerupCollected.listen([this](int p, int t) { TraceLog(LOG_INFO, "EVENT t=%.1f P%d power-up %d", time, p + 1, t); });
    }
    if (!opt.startOnTitle || opt.autoplay) startRun();
    if (opt.ending) startEnding();
}

bool Game::storyOn() const { return opt.story && config().b("story.enabled", true); }

void Game::shutdown()
{
    audio.shutdown();
    story.unload();
    renderer.shutdown();
    art.unload();
    CloseWindow();
}

void Game::startRun()
{
    screen = Screen::Playing;
    era = std::max(0, std::min(art::LAST_ERA, opt.startEra));
    bar = 0;
    score = 0;
    time = 0;
    enemies.clear();
    frags.clear();
    powerups.clear();
    particles.clear();
    boss = Boss();
    bossBeatenThisEra = false;
    transActive = false;
    lostTimer = 0;
    slowTime = wallSlowTime = 0;
    markers.clear();
    spawner.reset(level, markers);
    for (int p = 0; p < 2; ++p) {
        Player& pl = players[p];
        bool joined = pl.joined;
        pl = Player();
        pl.index = p;
        pl.joined = joined;
        pl.spawnAt(4.0f + p * 1.2f, (float)LEVEL_H - 2);
    }
    for (int e = 1; e <= era; ++e) unlockAbilitiesFor(e);
    cam = GameCamera();
    cam.x = 12;
    cam.top = LEVEL_H - art::VIEW_TILES_H;
    cam.pixelLock = art::lowRes(era);
    cam.swing = art::SWING[era];
    wallX = -10;
    bestX = players[0].x;
    audio.setEra(era);
    events().eraChanged.emit(era);
    if (era > 0) spawner.forceChunk(introChunk(era));   // starting late: show the gate
    spawner.fill(level, cam.right() + 60, era, 0, !solo(), markers);
    spawnMarkers();
    heldBannerLen = 0;
    pendingCards.clear();
    story.clearBars();
    if (storyOn()) {
        // the first run starts with the prologue; a restart or a late start goes straight to the memory
        if (era == 0 && !prologueSeen_) { pendingCards.push_back(Story::PROLOGUE); prologueSeen_ = true; }
        pendingCards.push_back(era);
    }
    levelBanner(T("era_name_" + std::to_string(era)), T("start_hint"), 3.0f);
}

void Game::showBanner(const std::string& title, const std::string& sub, float seconds)
{
    banner = title;
    bannerSub = sub;
    bannerT = seconds;
    bannerLen = seconds;
}

// ------------------------------------------------------------------ main loop
void Game::run()
{
    while (!WindowShouldClose()) {
        float frameDt = std::min(GetFrameTime(), 0.1f);
        if (opt.maxFrames > 0 || opt.autoplay) frameDt = DT;   // tests run at a fixed rate
        // input is sampled once per frame; presses go to the first tick only
        if (screen == Screen::Playing) {
            for (int p = 0; p < 2; ++p) inputs_[p] = opt.autoplay ? botInput(p) : input.read(p);
        }
        firstTick_ = true;
        acc_ += frameDt;
        int ticks = 0;
        while (acc_ >= DT && ticks < 5) {
            update(DT);
            acc_ -= DT;
            ++ticks;
            firstTick_ = false;
            for (auto& in : inputs_) { in.jumpPressed = in.attackPressed = in.abilityPressed = in.any = false; }
        }
        audio.update(frameDt);

        // ---- draw
        float flash = 0, desat = 0;
        const bool storyScreen = screen == Screen::Ending || screen == Screen::Story;
        if (screen == Screen::Title) renderer.drawTitleBackground(*this, time);
        else if (!storyScreen) {
            renderer.drawWorld(*this);
            // a short white pop right at the switch; the sweep does the rest
            if (transActive) flash = 0.7f * std::max(0.0f, 1.0f - std::fabs(transT - Renderer::kSwitchAt) / 0.15f);
            if (lostTimer > 0) desat = std::min(1.0f, (1.5f - lostTimer) * 2);
            for (int p = 0; p < 2 && !solo(); ++p) (void)p;
        }
        BeginDrawing();
        ClearBackground(BLACK);
        if (!storyScreen) renderer.present(*this, flash, desat);
        if (screen == Screen::Playing && !pendingCards.empty()) {
            // a memory card is due: photograph the world as it looks now (no HUD), then open it
            // Read the real framebuffer (render size, which differs from the window size under
            // display scaling) and crop it on whole pixels, clamped inside the image.
            rlDrawRenderBatchActive();
            const int rw = GetRenderWidth(), rh = GetRenderHeight();
            Image photo{rlReadScreenPixels(rw, rh), rw, rh, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
            Rectangle r = renderer.screenRect();
            float sx = rw / (float)std::max(1, GetScreenWidth()), sy = rh / (float)std::max(1, GetScreenHeight());
            int x0 = std::clamp((int)std::floor(r.x * sx), 0, rw), y0 = std::clamp((int)std::floor(r.y * sy), 0, rh);
            int x1 = std::clamp((int)std::floor((r.x + r.width) * sx), x0, rw), y1 = std::clamp((int)std::floor((r.y + r.height) * sy), y0, rh);
            if (photo.data && x1 - x0 > 0 && y1 - y0 > 0) {
                ImageCrop(&photo, Rectangle{(float)x0, (float)y0, (float)(x1 - x0), (float)(y1 - y0)});
                story.setPhoto(era, photo);
            } else {
                UnloadImage(photo);
            }
            story.open(pendingCards);
            pendingCards.clear();
            screen = Screen::Story;
            audio.setMusicDuck(0.45f);
        }
        if (screen == Screen::Title) hud.drawTitle(*this);
        else if (screen == Screen::Playing) { hud.drawPlaying(*this); story.drawBars(*this); }
        else if (screen == Screen::Paused) { hud.drawPlaying(*this); story.drawBars(*this); hud.drawPause(*this); }
        else if (screen == Screen::Story) story.drawCard(*this);
        else story.drawEnding(*this);
        ++frameNo_;
        bool last = opt.maxFrames > 0 && frameNo_ >= opt.maxFrames;
        bool periodic = opt.shotEvery > 0 && frameNo_ % opt.shotEvery == 0 && !opt.shotPrefix.empty();
        if ((last && !opt.shot.empty()) || periodic) {
            rlDrawRenderBatchActive();
            Image shot = LoadImageFromScreen();
            if (last && !opt.shot.empty()) ExportImage(shot, opt.shot.c_str());
            if (periodic) {
                char name[512];
                std::snprintf(name, sizeof name, "%s_%05d.png", opt.shotPrefix.c_str(), frameNo_);
                ExportImage(shot, name);
            }
            UnloadImage(shot);
        }
        EndDrawing();
        if (last) break;
    }
}

void Game::update(float dt)
{
    time += dt;
    bannerT = std::max(0.0f, bannerT - dt);
    switch (screen) {
    case Screen::Title: updateTitle(); break;
    case Screen::Playing: story.updateBars(dt); updatePlaying(dt); break;
    case Screen::Paused: updatePause(); break;
    case Screen::Story: updateStory(dt); break;
    case Screen::Ending: updateEnding(dt); break;
    }
}

void Game::updateTitle()
{
    if (!firstTick_) return;
    PlayerInput p1 = input.read(0), p2 = input.read(1);
    if (p2.any && !players[1].joined) {
        players[1].joined = true;
        audio.play("join");
        return;
    }
    bool go = p1.any || IsKeyPressed(KEY_ENTER) || (players[1].joined && p2.jumpPressed);
    for (int k = KEY_A; k <= KEY_Z && !go; ++k) go = IsKeyPressed(k) && k != KEY_P;
    if (go) {
        audio.play("select");
        startRun();
    }
}

void Game::updatePause()
{
    if (!firstTick_) return;
    if (input.pausePressed()) { screen = Screen::Playing; audio.setMusicDuck(1); return; }
    if (input.menuUp()) { pauseItem = (pauseItem + 2) % 3; audio.play("select"); }
    if (input.menuDown()) { pauseItem = (pauseItem + 1) % 3; audio.play("select"); }
    if (input.menuSelect()) {
        audio.setMusicDuck(1);
        if (pauseItem == 0) screen = Screen::Playing;
        else if (pauseItem == 1) startRun();
        else { screen = Screen::Title; players[1].joined = false; time = 0; era = 0; audio.setEra(0); }
    }
}

void Game::updateEnding(float dt)
{
    endingT += dt;
    bool pressed = firstTick_ && !opt.autoplay && storyPressed();
    if (story.updateEnding(dt, pressed, opt.autoplay || opt.maxFrames > 0)) {
        screen = Screen::Title;
        era = 0;
        time = 0;
        audio.setEra(0);
    }
}

bool Game::storyPressed()
{
    PlayerInput p1 = input.read(0), p2 = input.read(1);
    return p1.any || p2.any || IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE) || input.pausePressed() || input.menuSelect();
}

void Game::updateStory(float dt)
{
    bool pressed = firstTick_ && !opt.autoplay && storyPressed();
    story.update(dt, pressed, opt.autoplay || opt.maxFrames > 0);   // test runs never wait for a button
    if (story.active()) return;
    screen = Screen::Playing;
    audio.setMusicDuck(1);
    if (heldBannerLen > 0) { showBanner(heldBanner, heldBannerSub, heldBannerLen); heldBannerLen = 0; }
}

void Game::levelBanner(const std::string& title, const std::string& sub, float seconds)
{
    if (!storyOn()) { showBanner(title, sub, seconds); return; }
    heldBanner = title;
    heldBannerSub = sub;
    heldBannerLen = seconds;
}

void Game::joinPlayer2()
{
    Player& p2 = players[1];
    p2 = Player();
    p2.index = 1;
    p2.joined = true;
    const Player& p1 = players[0];
    p2.spawnAt(p1.cx() - 1.0f, p1.bottom());
    p2.invuln = 2;
    for (int e = 1; e <= era; ++e) {
        int ab = abilityForEra(1, e);
        if (ab >= 0) p2.has[ab] = true;
    }
    // P1 stops borrowing P2's abilities now there are two of you
    for (int e = 1; e <= era; ++e) {
        int ab = abilityForEra(1, e);
        if (ab >= 0) players[0].has[ab] = false;
    }
    audio.play("join");
    particles.burst(p2.cx(), p2.cy(), 30, P_SPARK[1], 6, 0.8f, 0.15f);
    showBanner(T("p2_joined"), T("p2_joined_sub"), 2.5f);
    events().playerJoined.emit(1);
}

// ------------------------------------------------------------------ gameplay
void Game::updatePlaying(float dt)
{
    if (firstTick_ && input.pausePressed() && !opt.autoplay) {
        screen = Screen::Paused;
        pauseItem = 0;
        audio.setMusicDuck(0.35f);
        return;
    }
    if (firstTick_ && !players[1].joined && !opt.autoplay && input.read(1).any) joinPlayer2();
    particles.update(dt);

    // hit-pause: a tiny freeze on big hits for punch
    if (hitPause > 0) { hitPause -= dt; return; }

    // ---- era transition: gameplay is frozen while the world upgrades
    if (transActive) {
        transT += dt;
        if (!transSwitched && transT >= Renderer::kSwitchAt) switchEra(transFromEra + 1);
        if (!transSwitched) cam.addShake(0.4f * transT / Renderer::kSwitchAt);   // the old world shudders
        if (transSwitched && art::SWING[era] != art::SWING[transFromEra]) {
            float k = std::min(1.0f, std::max(0.0f, (transT - 0.9f) / 2.6f));
            cam.swing = art::SWING[transFromEra] + (art::SWING[era] - art::SWING[transFromEra]) * k;
        }
        cam.pixelLock = art::lowRes(era) && cam.swing <= 0.001f;
        cam.update(cam.x, cam.top, 0, std::max(wallX - 3, (float)level.firstCol()), dt);
        if (transT >= transLen) {
            transActive = false;
            cam.swing = art::SWING[era];
            if (storyOn()) pendingCards = {era};   // the next memory's card, photographed in its new look
        }
        return;
    }

    // ---- memory lost: the world goes grey, then both respawn together
    if (lostTimer > 0) {
        lostTimer -= dt;
        if (lostTimer <= 0) respawnAfterLoss();
        return;
    }

    const bool soloNow = solo();
    const bool attackUnlocked = era >= config().i("player.attack_from_era", 1);
    const float slowK = slowTime > 0 ? config().f("abilities.time_slow_factor", 0.3f) : 1.0f;
    slowTime = std::max(0.0f, slowTime - dt);
    wallSlowTime = std::max(0.0f, wallSlowTime - dt);

    // ---- players
    for (int p = 0; p < 2; ++p) {
        Player& pl = players[p];
        if (!pl.joined) continue;
        pl.update(inputs_[p], level, soloNow, attackUnlocked, dt, ptune);
        if (pl.evJump) audio.play("jump", 0.7f);
        if (pl.evDoubleJump) { audio.play("double_jump", 0.8f); particles.burst(pl.cx(), pl.bottom(), 10, P_SPARK[p], 4, 0.4f, 0.12f); }
        if (pl.evWallJump) { audio.play("wall_jump", 0.8f); particles.burst(pl.cx() + pl.facing * -0.4f, pl.cy(), 8, WHITE, 3, 0.3f, 0.1f); }
        if (pl.evDash) { audio.play("dash"); particles.burst(pl.cx(), pl.cy(), 12, P_SPARK[p], 5, 0.3f, 0.12f); }
        if (pl.evCrack) { audio.play("crack"); cam.addShake(0.6f); particles.burst(pl.cx(), pl.bottom() + 0.5f, 25, Color{200, 180, 160, 255}, 7, 0.7f, 0.18f, 20); }
        if (pl.evAttack) audio.play("attack", 0.8f);
        if (pl.evSlowRequest) {
            slowTime = config().f("abilities.time_slow_duration", 3.0f);
            pl.slowCooldown = config().f("abilities.time_slow_cooldown", 10.0f);
            audio.play("slow");
            particles.burst(pl.cx(), pl.cy(), 40, Color{130, 230, 255, 255}, 10, 0.8f, 0.14f);
        }
        if (pl.evPoundLand) {
            audio.play("pound");
            cam.addShake(0.8f);
            hitPause = 0.05f;
            particles.burst(pl.cx(), pl.bottom(), 24, Color{220, 210, 190, 255}, 8, 0.5f, 0.16f, 15);
            for (auto& e : enemies) {
                if (e.dead || (e.owner >= 0 && e.owner != p && !soloNow)) continue;
                if (std::fabs(e.cx() - pl.cx()) < 2.6f && std::fabs((e.y + e.h) - pl.bottom()) < 1.6f) damageEnemy(e, 3, p, 0);
            }
            if (boss.active && boss.vulnerable() && std::fabs(boss.x - pl.cx()) < 2.8f) {
                boss.hp = std::max(0, boss.hp - 2);
                boss.hurt = 0.6f;
                audio.play("boss_hit");
            }
        }
        // trail of dust when running in the later eras
        if (era >= 3 && pl.onGround && std::fabs(pl.vx) > 4 && ((int)(time * 30) + p) % 6 == 0)
            particles.burst(pl.cx() - pl.facing * 0.3f, pl.bottom() - 0.05f, 1, Color{220, 210, 190, 150}, 1, 0.35f, 0.12f, -1);
    }

    // ---- attack hits
    for (int p = 0; p < 2; ++p) {
        Player& pl = players[p];
        if (!pl.alive() || pl.attackTime <= 0) continue;
        float ax = pl.facing > 0 ? pl.x + pl.w : pl.x - 1.3f;
        for (auto& e : enemies) {
            if (e.dead || e.hurt > 0 || (e.owner >= 0 && e.owner != p && !soloNow)) continue;
            if (overlap(ax, pl.y, 1.3f, pl.h, e.x, e.y, e.w, e.h)) damageEnemy(e, 1, p, pl.facing * 4.0f);
        }
        if (boss.active && boss.vulnerable() && overlap(ax, pl.y, 1.3f, pl.h, boss.x - boss.w / 2, boss.y - boss.h / 2, boss.w, boss.h)) {
            boss.hp = std::max(0, boss.hp - 1);
            boss.hurt = 0.6f;
            audio.play("boss_hit");
            cam.addShake(0.4f);
        }
    }

    // ---- enemies
    for (auto& e : enemies) updateEnemy(e, level, players, dt, tier(), slowK, etune);
    enemies.erase(std::remove_if(enemies.begin(), enemies.end(), [&](const Enemy& e) {
        return e.remove || e.x < wallX - 6;
    }), enemies.end());

    // ---- player vs enemy: stomp, dash, or get hurt
    for (int p = 0; p < 2; ++p) {
        Player& pl = players[p];
        if (!pl.alive()) continue;
        for (auto& e : enemies) {
            if (e.dead || !pl.alive()) continue;
            if (e.owner >= 0 && e.owner != p && !soloNow) continue;   // the other player's colour: pass through
            if (!overlap(pl.x, pl.y, pl.w, pl.h, e.x, e.y, e.w, e.h)) continue;
            float prevBottom = pl.bottom() - pl.vy * dt;
            bool stomp = (pl.vy > 0 || pl.pounding) && prevBottom <= e.y + 0.35f;
            if (stomp) {
                damageEnemy(e, pl.pounding ? 3 : 1, p, 0);
                pl.vy = -ptune.stompBounce * (inputs_[p].jumpHeld ? 1.25f : 1.0f);
                pl.pounding = false;
                pl.airJumps = 0;
                audio.play("stomp");
            } else if (pl.dashTime > 0) {
                damageEnemy(e, e.type == EN_BRUTE ? 1 : 3, p, pl.facing * 6.0f);
            } else if (pl.invuln <= 0) {
                hurtPlayer(p, false);
            }
        }
        // boss contact
        if (boss.active && boss.state != 5 && boss.state != 0 && pl.alive()) {
            float bx = boss.x - boss.w / 2, by = boss.y - boss.h / 2;
            if (overlap(pl.x, pl.y, pl.w, pl.h, bx, by, boss.w, boss.h)) {
                float prevBottom = pl.bottom() - pl.vy * dt;
                if (boss.vulnerable() && pl.vy > 0 && prevBottom <= by + 0.6f) {
                    boss.hp = std::max(0, boss.hp - (pl.pounding ? 2 : 1));
                    boss.hurt = 0.7f;
                    pl.vy = -ptune.stompBounce * 1.3f;
                    pl.pounding = false;
                    audio.play("boss_hit");
                    cam.addShake(0.7f);
                    hitPause = 0.08f;
                    particles.burst(boss.x, by, 30, Color{200, 120, 255, 255}, 8, 0.7f, 0.2f);
                } else if (boss.dangerous() && pl.invuln <= 0 && pl.dashTime <= 0) {
                    hurtPlayer(p, false);
                } else if (!boss.dangerous()) {
                    // touching a resting boss from the side just pushes you away
                    pl.vx = (pl.cx() < boss.x ? -1.0f : 1.0f) * 9.0f;
                    pl.x += (pl.cx() < boss.x ? -1.0f : 1.0f) * 0.1f;
                    if (pl.onGround) pl.vy = -6;
                }
            }
        }
    }

    // ---- boss
    if (boss.active) {
        boss.update(dt, cam.left(), cam.right(), level, players, tier(), slowK);
        if (boss.landed && boss.kind == 2) {
            // the school bell rings as it lands: anyone standing close is thrown back (no harm done)
            for (auto& pl : players) {
                if (!pl.alive() || !pl.onGround) continue;
                float dx = pl.cx() - boss.x;
                if (std::fabs(dx) > 4.5f) continue;
                pl.vx = (dx < 0 ? -1.0f : 1.0f) * 11.0f;
                pl.vy = -9.0f;
                pl.onGround = false;
                pl.wallLock = 0.25f;   // keeps the push: no jump cut, no steering for a moment
            }
            cam.addShake(0.8f);
            audio.play("pound");
            particles.burst(boss.x, boss.y + boss.h / 2, 40, Color{255, 220, 120, 255}, 9, 0.6f, 0.2f);
        }
        if (boss.wantsSpawn) {
            int type = tier() >= 2 && (std::rand() % 3 == 0) ? EN_BRUTE : (std::rand() % 2 ? EN_FLYER : EN_BLOB);
            if (boss.spawnsFlyers()) type = EN_FLYER;   // the storm's rain, the scarecrow's crows
            Enemy e = makeEnemy(type, boss.x, boss.y, tier(), etune);
            e.y = boss.y;
            e.baseY = e.y;
            if (!solo() && era >= 1 && std::rand() % 3 == 0) e.owner = std::rand() % 2;
            enemies.push_back(e);
            particles.burst(boss.x, boss.y, 16, Color{160, 100, 220, 255}, 5, 0.5f, 0.15f);
        }
        if (boss.hp <= 0 && boss.state != 5) {
            boss.state = 5;
            boss.stateTime = 0;
            audio.play("boss_hit", 1, 0.6f);
            cam.addShake(1.2f);
            spawnFragments(boss.x, boss.y, 12, 1.5f);
            particles.burst(boss.x, boss.y, 80, Color{220, 180, 255, 255}, 12, 1.4f, 0.25f);
            score += 500;
            events().bossDefeated.emit();
            if (era < art::LAST_ERA && storyOn())
                story.showBars(T("story_she_remembers"), T("memory_title_" + std::to_string(era)),
                               T("remembered_" + std::to_string(era)), 5.0f);
        }
        if (boss.defeated) {
            boss.active = false;
            bossBeatenThisEra = true;
            if (era < art::LAST_ERA) beginEraTransition();
            else startEnding();
            return;
        }
    }

    // ---- fragments drift to the nearest player and fill the bar
    const float magnetR = config().f("memory.fragment_magnet_radius", 2.8f);
    for (auto& f : frags) {
        f.t += dt;
        const Player* best = nullptr;
        float bd = 1e9f;
        for (int p = 0; p < 2; ++p) {
            const Player& pl = players[p];
            if (!pl.alive()) continue;
            float d = std::hypot(pl.cx() - f.x, pl.cy() - f.y);
            float r = pl.magnetTime > 0 ? 14.0f : magnetR;
            if (d < r && d < bd) { bd = d; best = &pl; }
        }
        if (best && (f.t > 0.35f || f.placed)) {
            float dx = best->cx() - f.x, dy = best->cy() - f.y, d = std::max(0.01f, bd);
            float pull = 22.0f + (best->magnetTime > 0 ? 20.0f : 0.0f);
            f.vx += dx / d * pull * dt;
            f.vy += dy / d * pull * dt;
            f.vx *= 0.92f; f.vy *= 0.92f;
            f.floating = false;
        } else if (!f.floating) {
            f.vy += 18 * dt;
            f.vx *= 1 - 2 * dt;
            if (level.isSolidChar(level.tile((int)std::floor(f.x), (int)std::floor(f.y + 0.3f))) && f.vy > 0) { f.vy *= -0.4f; f.y = std::floor(f.y + 0.3f) - 0.31f; }
        }
        f.x += f.vx * dt;
        f.y += f.vy * dt;
        if (best && bd < 0.7f) {
            int p = best->index;
            float before = bar;
            if (!boss.active) bar = std::min(100.0f, bar + f.value * fillSpeed_);
            score += 10;
            f.collected = true;
            audio.play("fragment", 0.6f, 1.0f + std::min(0.5f, (bar - before) * 0.02f) + (std::rand() % 10) * 0.01f);
            particles.burst(f.x, f.y, 5, fragColor(era), 3, 0.3f, 0.1f);
            events().fragmentCollected.emit(p, f.value);
        }
    }
    frags.erase(std::remove_if(frags.begin(), frags.end(), [&](const Fragment& f) {
        return f.collected || (!f.placed && f.t > 12) || f.y > LEVEL_H + 2 || f.x < wallX - 2;
    }), frags.end());

    // ---- power-ups
    for (auto& pu : powerups) {
        pu.t += dt;
        for (int p = 0; p < 2; ++p) {
            Player& pl = players[p];
            if (pl.alive() && overlap(pl.x, pl.y, pl.w, pl.h, pu.x - 0.45f, pu.y - 0.45f, 0.9f, 0.9f)) {
                collectPowerUp(p, pu);
                pu.t = -999;
                break;
            }
        }
    }
    powerups.erase(std::remove_if(powerups.begin(), powerups.end(), [&](const PowerUp& p) {
        return p.t < -100 || p.x < wallX - 2;
    }), powerups.end());

    // ---- the Forgetting Wall
    float lead = std::max(players[0].joined ? players[0].x : -1e9f, players[1].joined ? players[1].x : -1e9f);
    {
        // the old four-era scale: seven stages cover the same speed range
        float speed = config().f("wall.base_speed", 1.6f) + config().f("wall.speed_per_era", 0.45f) * era * 3.0f / art::LAST_ERA;
        float gapNow = lead - wallX;
        float catchDist = config().f("wall.catchup_distance", 24.0f);
        if (gapNow > catchDist) speed += (gapNow - catchDist) * config().f("wall.catchup_strength", 0.25f);
        if (slowTime > 0) speed *= slowK;
        if (wallSlowTime > 0) speed *= config().f("powerups.hourglass_slow_factor", 0.3f);
        if (time < config().f("wall.start_delay", 4.0f)) speed = 0;
        wallSpeedNow = speed;
        wallX += speed * dt;
    }
    for (int p = 0; p < 2; ++p) {
        Player& pl = players[p];
        if (!pl.joined) continue;
        if (pl.x < wallX) {
            if (pl.alive() && pl.invuln <= 0) hurtPlayer(p, true);
            pl.x = std::max(pl.x, wallX + 0.05f);   // the wall pushes Faded players along
            if (boxHitsSolid(level, pl.x, pl.y, pl.w, pl.h, p, soloNow)) pl.y -= 1.0f;
        }
    }

    // ---- falling out of the world, being left off-screen, reviving
    for (int p = 0; p < 2; ++p) {
        Player& pl = players[p];
        if (!pl.joined) continue;
        Player& other = players[1 - p];
        if (pl.y > LEVEL_H + 1) {
            if (other.alive()) {
                if (!pl.faded) fadePlayer(p);
                pl.spawnAt(other.cx(), other.y - 0.5f);
                pl.vy = -4;
            } else if (soloNow) {
                if (!pl.faded) fadePlayer(p);
            } else {
                if (!pl.faded) fadePlayer(p);
                pl.spawnAt(cam.x, 2);
            }
        }
        bool off = pl.cx() < cam.left() - 0.5f || pl.cx() > cam.right() + 0.5f;
        pl.offscreen = off ? pl.offscreen + dt : 0;
        if (pl.offscreen > config().f("coop.offscreen_seconds", 2.0f) && other.alive()) {
            pl.offscreen = 0;
            if (!pl.faded) fadePlayer(p);
            pl.spawnAt(other.cx(), other.y);
        }
        // revive: the partner touches the Faded player for a moment
        if (pl.faded && other.alive()) {
            bool touch = overlap(pl.x - 0.35f, pl.y - 0.35f, pl.w + 0.7f, pl.h + 0.7f, other.x, other.y, other.w, other.h);
            if (touch) {
                pl.reviveProgress += dt;
                if (((int)(pl.reviveProgress * 30)) % 3 == 0) particles.burst(pl.cx(), pl.cy(), 1, WHITE, 2, 0.4f, 0.1f);
            } else pl.reviveProgress = std::max(0.0f, pl.reviveProgress - dt * 0.5f);
            if (pl.reviveProgress >= config().f("coop.revive_seconds", 0.6f)) revivePlayer(p);
        }
    }
    // everyone Faded: the memory slips
    bool anyAlive = players[0].alive() || (players[1].joined && players[1].alive());
    if (!anyAlive) { memoryLost(); return; }

    // ---- camera: midpoint of the players, never behind the wall
    float minX = 1e9f, maxX = -1e9f;
    for (auto& pl : players)
        if (pl.joined) { minX = std::min(minX, pl.cx()); maxX = std::max(maxX, pl.cx()); }
    float target = (minX + maxX) / 2 + 2.0f;
    // up and down: keep the highest player (and the boss) comfortably inside the view
    float highest = 1e9f;
    for (auto& pl : players)
        if (pl.joined) highest = std::min(highest, pl.y);
    float wantTop = highest - 3.0f;
    if (boss.active) wantTop = std::min(wantTop, boss.y - boss.h / 2 - 1.0f);
    cam.pixelLock = art::lowRes(era) && cam.swing <= 0.001f;
    // never show past the start of the level: no empty strip at the left edge
    cam.update(target, wantTop, maxX - minX, std::max(wallX - 3.0f, (float)level.firstCol()), dt);

    // ---- endless level: keep ~3 chunks ahead, drop what is far behind the wall
    unsigned mask = 0;
    for (auto& pl : players)
        if (pl.joined)
            for (int a = 0; a < AB_COUNT; ++a)
                if (pl.has[a]) mask |= 1u << a;
    if (level.endCol() < cam.right() + 70) {
        size_t before = markers.size();
        spawner.fill(level, cam.right() + config().f("level.lookahead_tiles", 30.0f), era, mask, !soloNow, markers);
        if (markers.size() != before) spawnMarkers();
    }
    level.dropBefore((int)std::floor(wallX) - 30);

    // extra flyers drift in as the eras go by (spawn rate scales with era)
    static float spawnClock = 0;
    spawnClock += dt;
    float every = config().f("enemies.extra_spawn_seconds", 9.0f) / (1.0f + tier() * 0.35f);
    if (spawnClock > every && !boss.active) {
        spawnClock = 0;
        Enemy e = makeEnemy(EN_FLYER, cam.right() + 2, 3.0f + (std::rand() % 4), tier(), etune);
        if (!soloNow && era >= 1 && std::rand() % 4 == 0) e.owner = std::rand() % 2;
        enemies.push_back(e);
    }

    // ---- passive fill from distance (small; kills are the main source)
    if (lead > bestX) {
        bar = boss.active ? bar : std::min(100.0f, bar + (lead - bestX) * config().f("memory.passive_per_tile", 0.03f) * fillSpeed_);
        bestX = lead;
    }
    if (opt.fill && !boss.active) bar = std::min(100.0f, bar + dt * 12);

    // ---- bar full: the Forgetting gathers itself (mini-boss), then the memory returns
    if (bar >= 100.0f && !boss.active && !transActive) {
        if (config().b("boss.enabled", true) && !bossBeatenThisEra) {
            boss.start(cam.x, tier(), config().i("boss.base_hits", 2), config().i("boss.hits_per_era", 1), era);
            audio.play("boss_appear");
            cam.addShake(0.6f);
            if (storyOn()) story.showBars(T("story_boss_label"), T("boss_name_" + std::to_string(era)), T("boss_says_" + std::to_string(era)), 3.4f);
            else showBanner(T("boss_name_" + std::to_string(era)), T("boss_hint_short"), 2.5f);
            events().bossAppeared.emit();
        } else if (era < art::LAST_ERA) beginEraTransition();
        else startEnding();
    }
}

// ------------------------------------------------------------------ helpers
void Game::spawnMarkers()
{
    for (const auto& m : markers) {
        switch (m.c) {
        case 'S': enemies.push_back(makeEnemy(EN_BLOB, m.x, m.y, tier(), etune)); break;
        case 'G': enemies.push_back(makeEnemy(EN_FLYER, m.x, m.y, tier(), etune)); break;
        case 'B':
            // Fog Brutes appear from the 8-bit era on; before that a Static Blob stands in
            enemies.push_back(makeEnemy(era >= 1 ? EN_BRUTE : EN_BLOB, m.x, m.y, tier(), etune));
            break;
        case 'a':
        case 'b': {
            Enemy e = makeEnemy(EN_BLOB, m.x, m.y, tier(), etune);
            e.owner = solo() ? -1 : (m.c == 'a' ? 0 : 1);
            enemies.push_back(e);
            break;
        }
        case 'F': {
            Fragment f;
            f.x = m.x + 0.5f;
            f.y = m.y + 0.5f;
            f.floating = true;
            f.placed = true;
            f.value = config().f("memory.fragment_value", 1.0f) * (opt.fill ? 5 : 1);
            frags.push_back(f);
            break;
        }
        case 'P': {
            int w[PU_COUNT];
            int total = 0;
            for (int i = 0; i < PU_COUNT; ++i) {
                w[i] = config().i("powerups.weight_" + std::to_string(i), 1);
                total += w[i];
            }
            int r = total > 0 ? std::rand() % total : 0, type = 0;
            for (int i = 0; i < PU_COUNT; ++i) { r -= w[i]; if (r < 0) { type = i; break; } }
            powerups.push_back({type, m.x + 0.5f, m.y + 0.5f, 0});
            break;
        }
        }
    }
    markers.clear();
}

void Game::spawnFragments(float x, float y, int n, float spread)
{
    float v = config().f("memory.fragment_value", 1.0f) * (opt.fill ? 5 : 1);
    for (int i = 0; i < n; ++i) {
        Fragment f;
        f.x = x + randf(-spread, spread) * 0.3f;
        f.y = y;
        f.vx = randf(-3, 3) * spread;
        f.vy = randf(-9, -5);
        f.value = v;
        frags.push_back(f);
    }
}

void Game::damageEnemy(Enemy& e, int dmg, int byPlayer, float knock)
{
    if (e.dead) return;
    e.hp -= dmg;
    e.hurt = 0.3f;
    e.x += knock * 0.08f;
    if (e.hp <= 0) killEnemy(e, byPlayer);
    else {
        audio.play("hit");
        cam.addShake(0.25f);
        particles.burst(e.cx(), e.cy(), 8, Color{200, 190, 230, 255}, 4, 0.4f, 0.12f);
    }
}

void Game::killEnemy(Enemy& e, int byPlayer)
{
    e.dead = true;
    e.deadTime = 0;
    int n = etune.drops[e.type];
    spawnFragments(e.cx(), e.cy(), n, e.type == EN_BRUTE ? 1.2f : 0.7f);
    score += 50 * n;
    Color c = era <= 1 ? Color{235, 235, 225, 255} : (e.type == EN_FLYER ? Color{240, 80, 110, 255} : Color{150, 140, 200, 255});
    particles.burst(e.cx(), e.cy(), 14 + n * 3, c, 6, 0.5f, 0.14f, 6);
    if (e.type == EN_BRUTE) { cam.addShake(0.5f); hitPause = 0.06f; }
    if (std::rand() % 100 < config().i("powerups.drop_percent", 6))
        powerups.push_back({std::rand() % PU_COUNT, e.cx(), e.cy() - 0.5f, 0});
    events().enemyKilled.emit(e.type, e.cx(), e.cy());
    (void)byPlayer;
}

void Game::hurtPlayer(int p, bool fromWall)
{
    Player& pl = players[p];
    if (!pl.alive()) return;
    if (pl.shield && !fromWall) {
        pl.shield = false;
        pl.invuln = 1.5f;
        pl.vy = -10;
        pl.vx = -pl.facing * 6;
        audio.play("hit");
        particles.burst(pl.cx(), pl.cy(), 20, Color{255, 120, 160, 255}, 6, 0.5f, 0.14f);
        return;
    }
    fadePlayer(p);
    pl.vy = -8;
    cam.addShake(0.5f);
}

void Game::fadePlayer(int p)
{
    Player& pl = players[p];
    pl.faded = true;
    pl.fadedTime = 0;
    pl.reviveProgress = 0;
    pl.dashTime = 0;
    pl.pounding = false;
    pl.shield = false;
    particles.burst(pl.cx(), pl.cy(), 24, Color{200, 200, 210, 255}, 5, 0.8f, 0.14f, -2);
    events().playerFaded.emit(p);
}

void Game::revivePlayer(int p)
{
    Player& pl = players[p];
    pl.faded = false;
    pl.reviveProgress = 0;
    pl.invuln = config().f("coop.invuln_after_revive", 2.0f);
    if (pl.x < wallX + 1) pl.x = players[1 - p].x;
    particles.burst(pl.cx(), pl.cy(), 40, P_SPARK[p], 7, 0.9f, 0.16f);
    score += 100;
    events().playerRevived.emit(p);
}

void Game::memoryLost()
{
    lostTimer = 1.5f;
    audio.play("memory_lost");
    showBanner(T("memory_lost"), solo() ? T("memory_lost_sub_solo") : T("memory_lost_sub"), 2.6f);
    events().memoryLost.emit();
}

void Game::respawnAfterLoss()
{
    if (!boss.active) bar = std::max(0.0f, bar - config().f("memory.lost_penalty", 10.0f));
    // find solid ground a little ahead of the wall
    int x = (int)std::max(cam.x - 3, wallX + 10);
    int gt = -1;
    for (int k = 0; k < 40 && gt < 0; ++k, ++x) gt = level.groundTop(x);
    if (gt < 0) gt = LEVEL_H - 2;
    for (int p = 0; p < 2; ++p) {
        Player& pl = players[p];
        if (!pl.joined) continue;
        pl.faded = false;
        pl.reviveProgress = 0;
        pl.spawnAt(x + 0.5f + p * 0.8f, (float)gt);
        pl.invuln = 2.5f;
        particles.burst(pl.cx(), pl.cy(), 30, P_SPARK[p], 6, 0.8f, 0.15f);
    }
    wallX = std::min(wallX, (float)x - config().f("wall.respawn_gap", 14.0f));
    enemies.erase(std::remove_if(enemies.begin(), enemies.end(), [&](const Enemy& e) {
        return std::fabs(e.cx() - x) < 5;
    }), enemies.end());
    if (boss.active && boss.state != 5) { boss.state = 1; boss.stateTime = 0; }
}

void Game::collectPowerUp(int p, PowerUp& pu)
{
    Player& pl = players[p];
    audio.play("powerup");
    particles.burst(pu.x, pu.y, 26, P_SPARK[p], 6, 0.6f, 0.15f);
    switch (pu.type) {
    case PU_SHIELD: pl.shield = true; break;
    case PU_SLOW:
        wallX -= config().f("powerups.hourglass_push", 8.0f);
        wallSlowTime = config().f("powerups.hourglass_seconds", 6.0f);
        break;
    case PU_MAGNET: pl.magnetTime = config().f("powerups.magnet_seconds", 10.0f); break;
    case PU_PHOTO:
        if (!boss.active) bar = std::min(100.0f, bar + config().f("powerups.photo_value", 8.0f) * fillSpeed_);
        score += 200;
        break;
    case PU_SPEED: pl.speedTime = config().f("powerups.speed_seconds", 8.0f); break;
    }
    showBanner(T("powerup_" + std::to_string(pu.type)), T("powerup_desc_" + std::to_string(pu.type)), 1.6f);
    events().powerupCollected.emit(p, pu.type);
}

// ------------------------------------------------------------------ eras
void Game::beginEraTransition()
{
    transActive = true;
    transT = 0;
    transSwitched = false;
    transFromEra = era;
    transLen = art::SWING[era + 1] != art::SWING[era] ? 4.4f : 3.4f;   // the camera swing takes longer
    audio.play("era_up");
    TraceLog(LOG_INFO, "UPGRADE: era transition %d -> %d at frame %d", era, era + 1, frameNo_);
}

void Game::unlockAbilitiesFor(int newEra)
{
    for (int p = 0; p < 2; ++p) {
        int ab = abilityForEra(p, newEra);
        if (ab < 0) continue;
        // playing alone, Player 1 gets both sets so every chunk stays passable
        int who = (p == 1 && solo()) ? 0 : p;
        players[who].has[ab] = true;
        spawner.forceAbility(ab);
        events().abilityUnlocked.emit(who, abilityId(ab));
    }
}

void Game::switchEra(int newEra)
{
    transSwitched = true;
    era = std::min(art::LAST_ERA, newEra);
    bar = 0;
    bossBeatenThisEra = false;
    unlockAbilitiesFor(era);
    // The level just past the screen is rebuilt so the new era's intro gate comes next.
    {
        int cut = (int)std::ceil(cam.right()) + 8;
        if (level.endCol() > cut) {
            float cx = (float)cut;
            level.truncateFrom(cut);
            enemies.erase(std::remove_if(enemies.begin(), enemies.end(), [cx](const Enemy& e) { return e.cx() >= cx; }), enemies.end());
            frags.erase(std::remove_if(frags.begin(), frags.end(), [cx](const Fragment& f) { return f.placed && f.x >= cx; }), frags.end());
            powerups.erase(std::remove_if(powerups.begin(), powerups.end(), [cx](const PowerUp& p) { return p.x >= cx; }), powerups.end());
            markers.erase(std::remove_if(markers.begin(), markers.end(), [cx](const SpawnMarker& m) { return m.x >= cx; }), markers.end());
        }
        unsigned mask = 0;
        for (auto& pl : players)
            if (pl.joined)
                for (int a = 0; a < AB_COUNT; ++a)
                    if (pl.has[a]) mask |= 1u << a;
        spawner.forceChunk(introChunk(era));
        size_t before = markers.size();
        spawner.fill(level, cam.right() + config().f("level.lookahead_tiles", 30.0f), era, mask, !solo(), markers);
        if (markers.size() != before) spawnMarkers();
    }
    audio.setEra(era);
    audio.play("unlock");
    events().eraChanged.emit(era);
    // only the abilities this stage brings (and the attack button when it unlocks)
    std::string sub;
    for (int p = 0; p < 2; ++p) {
        int ab = abilityForEra(p, era);
        if (ab < 0) continue;
        sub += std::string(sub.empty() ? "" : "   ") + T(solo() ? "p1_name" : (p == 0 ? "p1_name" : "p2_name")) + ": " +
               T(std::string("ability_") + abilityId(ab));
    }
    if (era == config().i("player.attack_from_era", 2)) sub += std::string(sub.empty() ? "" : "   ") + T("ability_attack");
    if (!sub.empty()) sub = std::string(T("new_ability")) + "  " + sub;
    // the level banner: the new stage and what it brings. With the story on, it waits for the card.
    if (storyOn()) levelBanner(T("era_name_" + std::to_string(era)), sub, 3.0f);
    else showBanner(T("era_name_" + std::to_string(era)), sub, transLen - 0.6f + 1.5f);
    particles.burst(cam.x, 7, 120, WHITE, 14, 1.2f, 0.2f);
    for (auto& pl : players) if (pl.joined && pl.faded) { pl.faded = false; pl.invuln = 2; }
}

void Game::startEnding()
{
    screen = Screen::Ending;
    endingT = 0;
    story.clearBars();
    story.startEnding();
    era = art::LAST_ERA;
    audio.setEra(era);
    audio.play("unlock");
    events().memoryComplete.emit();
}

// ------------------------------------------------------------------ autoplay bot (for tests)
PlayerInput Game::botInput(int p)
{
    PlayerInput in;
    const Player& pl = players[p];
    if (!pl.joined) return in;
    const Player& other = players[1 - p];
    in.right = true;
    // P2 bot stays near P1 to revive them
    if (p == 1 && other.joined && other.faded) { in.right = other.cx() > pl.cx(); in.left = !in.right; }
    if (p == 0 && players[1].joined && players[1].faded) { in.right = players[1].cx() > pl.cx(); in.left = !in.right; }
    int fx = (int)std::floor(pl.cx() + pl.facing * 1.2f);
    int feet = (int)std::floor(pl.bottom() + 0.1f);
    bool wallAhead = level.solidFor(fx, (int)std::floor(pl.cy()), p, solo()) || level.solidFor(fx, (int)std::floor(pl.bottom() - 0.2f), p, solo());
    bool gapAhead = true;
    for (int dy = 0; dy < 4; ++dy)
        if (level.solidFor(fx, feet + dy, p, solo()) || level.solidFor(fx + 1, feet + dy, p, solo())) gapAhead = false;
    bool enemyAhead = false;
    for (const auto& e : enemies)
        if (!e.dead && e.cx() > pl.cx() && e.cx() - pl.cx() < 2.2f && std::fabs(e.cy() - pl.cy()) < 1.5f) enemyAhead = true;
    static int hold[2] = {0, 0};
    if ((wallAhead || gapAhead || enemyAhead) && pl.onGround) { in.jumpPressed = true; hold[p] = 20; }
    if (!pl.onGround && pl.vy > 2 && gapAhead && (pl.has[AB_DOUBLE_JUMP] && pl.airJumps == 0)) { in.jumpPressed = true; hold[p] = 15; }
    if (!pl.onGround && pl.wallDir != 0 && pl.has[AB_WALL_JUMP] && pl.vy > 0) { in.jumpPressed = true; hold[p] = 12; }
    if (hold[p] > 0) { in.jumpHeld = true; --hold[p]; }
    if (pl.has[AB_GLIDE] && !pl.onGround && pl.vy > 0 && gapAhead) in.jumpHeld = true;
    if (pl.has[AB_DASH] && !pl.onGround && gapAhead && pl.vy > 0 && pl.dashCooldown <= 0) in.abilityPressed = true;
    if (enemyAhead) in.attackPressed = true;
    // smash cracked floors when standing on them
    if (pl.has[AB_GROUND_POUND] && !pl.onGround && level.tile((int)std::floor(pl.cx()), feet + 1) == '=') { in.abilityPressed = true; in.down = true; }
    if (pl.has[AB_TIME_SLOW] && pl.slowCooldown <= 0 && boss.active) { in.abilityPressed = true; in.down = true; }
    // dodge the swoop marker, then jump on the boss when it rests
    if (boss.active && boss.aiming() && std::fabs(boss.toX - pl.cx()) < 3.0f) {
        in.right = pl.cx() > boss.toX;
        in.left = !in.right;
    }
    if (boss.active && boss.state == 3) {
        in.right = boss.x > pl.cx();
        in.left = !in.right;
        if (pl.onGround && std::fabs(boss.x - pl.cx()) < 3.5f) { in.jumpPressed = true; hold[p] = 25; }
    }
    if (std::rand() % 90 == 0 && pl.onGround) { in.jumpPressed = true; hold[p] = 10; }
    return in;
}
