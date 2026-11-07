// Game.h: game state and the main loop at a fixed 60 updates per second.
// Screens: title -> playing (with era transitions and pause) -> ending.
#pragma once
#include "audio/Audio.h"
#include "enemies/Enemy.h"
#include "level/Level.h"
#include "player/Input.h"
#include "player/Player.h"
#include "render/Art.h"
#include "render/GameCamera.h"
#include "render/Particles.h"
#include "render/Renderer.h"
#include "ui/Cutscene.h"
#include "ui/Hud.h"

#include <random>
#include <string>
#include <vector>

enum class Screen { Title, Playing, Paused, Ending, Settings };

enum PowerType { PU_SHIELD, PU_SLOW, PU_MAGNET, PU_PHOTO, PU_SPEED, PU_COUNT };

struct Fragment {
    float x, y, vx = 0, vy = 0, t = 0;
    float value = 1;
    bool floating = false;   // hangs in the air until a player comes near
    bool placed = false;     // placed in a chunk: never expires
    bool collected = false;
};

struct PowerUp {
    int type;
    float x, y, t = 0;
};

struct Options {
    bool autoplay = false, twoPlayers = false, fill = false, startOnTitle = true, ending = false;
    bool cutscenes = false;   // --cutscenes: play them even with --autoplay (auto-advance, for screenshots)
    int startEra = 0, maxFrames = 0, shotEvery = 0;
    unsigned seed = 0;
    std::string shot, shotPrefix;
    int width = 1280, height = 720;
    bool openSettings = false;                  // --settings: start on the Settings screen
    std::vector<std::string> setOverrides;      // --set key=value (settings), applied after load
};

class Game {
public:
    void init(const Options& opt);
    void run();
    void shutdown();

    // ---- everything below is shared with the renderer and HUD
    Options opt;
    Screen screen = Screen::Title;
    float time = 0;              // seconds since start (real)
    int era = 0;
    float bar = 0;               // Memory Bar for the current era, 0..100
    int score = 0;
    bool solo() const { return !players[1].joined; }

    Level level;
    ChunkSpawner spawner;
    std::vector<Chunk> chunks;
    std::vector<SpawnMarker> markers;
    Player players[2];
    std::vector<Enemy> enemies;
    Boss boss;
    bool bossBeatenThisEra = false;
    std::vector<Fragment> frags;
    std::vector<PowerUp> powerups;
    float wallX = -8, wallSpeedNow = 0, wallSlowTime = 0;
    float slowTime = 0;          // time-slow pulse remaining
    float lostTimer = 0;         // > 0 while "memory lost" plays out
    float bestX = 0;

    // era transition
    bool transActive = false;
    float transT = 0, transLen = 3.4f;
    bool transSwitched = false;
    int transFromEra = 0;

    // ending
    float endingT = 0;

    // pause menu
    int pauseItem = 0;

    // settings menu (akshat): reachable from the title and the pause menu
    int settingsItem = 0;                // row, 0..SET_COUNT (the last row is Back)
    Screen settingsFrom = Screen::Title; // where Back returns to
    void openSettings();
    void applySettings(bool window);     // push settings into raylib (window = fullscreen/vsync/fps too)

    // messages: a short banner in the middle of the screen
    std::string banner, bannerSub;
    float bannerT = 0, bannerLen = 0;

    Art art;
    Audio audio;
    InputMap input;
    PlayerTuning ptune;
    EnemyTuning etune;
    GameCamera cam;
    Particles particles;
    Renderer renderer;
    Hud hud;
    Cutscene cutscene;   // akshat: story scenes (assets/cutscenes.txt)
    float hitPause = 0;

    void startRun();
    void showBanner(const std::string& title, const std::string& sub, float seconds);

private:
    std::mt19937 rng_;
    float acc_ = 0;
    int frameNo_ = 0;
    PlayerInput inputs_[2];
    bool firstTick_ = true;

    void update(float dt);
    void updateTitle();
    void updatePlaying(float dt);
    void updatePause();
    void updateSettings();
    void updateEnding(float dt);
    PlayerInput botInput(int p);

    void spawnMarkers();
    void spawnFragments(float x, float y, int n, float spread);
    void killEnemy(Enemy& e, int byPlayer);
    void damageEnemy(Enemy& e, int dmg, int byPlayer, float knock);
    void hurtPlayer(int p, bool fromWall);
    void fadePlayer(int p);
    void revivePlayer(int p);
    void memoryLost();
    void respawnAfterLoss();
    void collectPowerUp(int p, PowerUp& pu);
    void beginEraTransition();
    void switchEra(int newEra);
    void unlockAbilitiesFor(int newEra);
    void startEnding();
    void joinPlayer2();
    float randf(float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng_); }
};
