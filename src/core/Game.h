// Game.h: game state and the main loop at a fixed 60 updates per second.
// Screens: title -> story card -> playing (with era transitions and pause) -> story card -> ... -> ending.
#pragma once
#include "audio/Audio.h"
#include "enemies/BossPowers.h"
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
#include "ui/Story.h"

#include <random>
#include <string>
#include <vector>

constexpr int TITLE_ITEMS = 6;   // rows of the title menu (ui/Hud.cpp draws them)

enum class Screen { Title, Playing, Paused, Story, Ending, Settings };

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
    bool show = false;       // akshat: --show, a test run in a visible window you can watch
    bool autoplay = false, twoPlayers = false, fill = false, startOnTitle = true, ending = false, story = true;
    int startEra = 0, maxFrames = 0, shotEvery = 0;
    int difficulty = -1;   // --difficulty; -1: use [boss] difficulty in config.ini
    bool cutscenes = false;   // --cutscenes: play boss scenes even with --autoplay (they auto-advance)
    unsigned seed = 0;
    std::string shot, shotPrefix;
    int width = 1280, height = 720;
    bool openSettings = false;                  // --settings: start on the Settings screen
    bool openPause = false;                     // --pause: start a run paused (UI screenshots)
    bool openCredits = false;                   // --credits: open the credits from the title
    int titleItem = 0;                          // --menu N: the title menu row to start on
    bool hitboxes = false;   // --hitboxes: draw every boss / player / shot hit box (debug)
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

    // title menu (akshat, UI pass): Start, Players, Difficulty, Settings, Credits, Quit
    int titleItem = 0;
    int titleMode = 0;           // 0 the menu, 1 the credits
    bool quitRequested = false;  // Quit on the title: run() ends after this frame

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
    Story story;
    Cutscene cutscene;   // akshat: Pokemon-style boss scenes (assets/cutscenes.txt)
    float hitPause = 0;

    // power-ups: the last one each player picked up, for the corner pop-up (seconds left, type)
    float puPopT[2] = {0, 0};
    int puPopType[2] = {0, 0};
    int hourglassBy = 0;          // who picked up the hourglass (its icon sits in their corner)

    // the bosses' powers over time (enemies/BossPowers.h, core/GamePowers.cpp)
    int difficulty = DIFF_GENTLE;   // chosen on the title screen
    BossPowerState powers;
    const PowerTuning& powerTuning() const;

    void startRun();
    void showBanner(const std::string& title, const std::string& sub, float seconds);
    bool storyOn() const;

    // story: the memory card to open once the next frame is drawn (its photograph is taken then)
    std::vector<int> pendingCards;
    // the level banner (stage name, new abilities) waits until the story card is closed
    std::string heldBanner, heldBannerSub;
    float heldBannerLen = 0;
    void levelBanner(const std::string& title, const std::string& sub, float seconds);

private:
    std::mt19937 rng_;
    float acc_ = 0;
    int frameNo_ = 0;
    PlayerInput inputs_[2];
    bool firstTick_ = true;
    bool prologueSeen_ = false;
    int lastPadCount_ = -1;   // -1: not counted yet (no notice for pads already in at start)
    struct Trace { float x, y; bool grounded; };
    std::vector<Trace> history_[2];      // where the players have been (for the pull back)
    std::vector<Vector2> pullPath_[2];
    float historyClock_ = 0;
    static constexpr float kPullTime = 0.7f;
    void startBossPowers();
    bool bossHeldByPower() const;
    void recordHistory(float dt);
    void clearHistory();
    void applyBossPower();
    void updateBossPowers(float dt);
    // the boss fight (akshat, boss rework; core/GameBoss.cpp)
    bool arenaOn_ = false;
    float arenaL_ = 0, arenaR_ = 0, arenaFloor_ = 12;
    float lastWeakHit_[2] = {-99, -99};
    float bossStartT_ = 0;                   // when the fight began (for the test log)
    int bossWipes_ = 0;
    bool swingUsed_[2] = {false, false};     // one weak-point hit / clank per swing
    void startBossFight();
    void bossPlayerCombat(float dt);
    bool updateBossFight(float dt, float slowK);   // true: the fight is over this frame (return)
    void hitBossWeak(int p, int dmg, const char* how);
    void keepPlayersInArena();
    void bossRespawn();
    bool botBoss(int p, PlayerInput& in, int& hold);

    void update(float dt);
    void updateTitle();
    void updatePlaying(float dt);
    void updatePause();
    void updateSettings();
    void updateEnding(float dt);
    void updateStory(float dt);
    bool storyPressed();
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
    void memoryRestored();     // the bar is full and any boss is beaten: upgrade (or the ending)
    bool bossStage(int e) const;   // [boss] stages: which stages have a boss
    void switchEra(int newEra);
    void unlockAbilitiesFor(int newEra);
    void startEnding();
    void joinPlayer2();
    float fillSpeed_ = 1.0f;   // memory.fill_speed: multiplies every way the bar fills
    int tier() const { return art::TIER[era]; }   // difficulty on the old four-era scale
    static std::string introChunk(int era) { return "intro_stage" + std::to_string(era); }
    float randf(float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng_); }
};
