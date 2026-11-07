// Enemy.h: the creatures of the Forgetting (brief section 8) and the era mini-boss.
//   Static Blob  - walks back and forth, turns at walls and ledges. Dies to a stomp or attack.
//   Glitch Flyer - floats in a sine wave and drifts toward the nearest player; glitches (blinks) from 16-bit on.
//   Fog Brute    - 3 hits, from the 8-bit era on. Walks; from 16-bit on it winds up and charges.
//   The Forgetting (boss) - appears when the Memory Bar is full. Hovers, swoops down at a player,
//                  rests on the ground where it can be stomped, then rises. Beat it to restore the memory.
//                  Each memory has its own boss (Boss::kind = the stage), with its own way of moving:
//                  0 the 3 a.m. clock ticks from place to place; 1 the empty shirt flutters wide and glides in;
//                  2 the school bell rings when it lands and knocks you back; 3 the storm strikes fast like
//                  lightning and rains flyers; 4 the last train drops at one side and charges along the ground;
//                  5 the scarecrow calls crows (flyers); 6 the Forgetting itself, as it always was.
#pragma once
#include <vector>

class Level;
struct Player;

enum EnemyType { EN_BLOB, EN_FLYER, EN_BRUTE, EN_TYPES };

struct EnemyTuning {
    float blobSpeed, flyerSpeed, bruteSpeed, bruteCharge, speedPerEra;
    int bruteHp;
    int drops[EN_TYPES];
    void load();
};

struct Enemy {
    int type = EN_BLOB;
    int owner = -1;            // -1 everyone; 0 / 1 only that player can see and hurt it
    float x = 0, y = 0, w = 0.8f, h = 0.75f;
    float vx = 0, vy = 0;
    int hp = 1;
    int dir = -1;
    float t = 0, baseY = 0, hurt = 0;
    int state = 0;             // brute: 0 walk, 1 wind-up, 2 charge
    float stateTime = 0;
    bool onGround = false;
    bool dead = false;
    float deadTime = 0;
    bool remove = false;

    float cx() const { return x + w / 2; }
    float cy() const { return y + h / 2; }
};

Enemy makeEnemy(int type, float tileX, float tileY, int era, const EnemyTuning& t);
// timeScale < 1 while the time-slow pulse is active.
void updateEnemy(Enemy& e, Level& level, const Player* players, float dt, int era, float timeScale, const EnemyTuning& t);

struct Boss {
    bool active = false;
    float x = 0, y = 0;        // centre
    float w = 2.4f, h = 2.2f;
    int hp = 3, maxHp = 3;
    int state = 0;             // 0 enter, 1 hover, 6 aim (warning marker), 2 swoop, 3 rest (vulnerable), 4 rise, 5 dying
    float t = 0, stateTime = 0, hurt = 0, spawnTimer = 0;
    float fromX = 0, fromY = 0, toX = 0, toY = 0;
    bool wantsSpawn = false;   // the Game spawns a minion when this is set
    bool defeated = false;
    int kind = 6;              // which memory's boss this is (the stage it appears in)
    bool landed = false;       // set for one update when a swoop ends on the ground
    bool charge = false;       // the train: this swoop drops, then charges along flat ground

    void start(float camX, int era, int hpBase, int hpPerEra, int kind);
    void update(float dt, float camLeft, float camRight, const Level& level, const Player* players, int era, float timeScale);
    bool vulnerable() const { return state == 3 && hurt <= 0; }
    bool dangerous() const { return state == 2; }        // only the swoop hurts; a resting boss just pushes you back
    bool aiming() const { return state == 6 || state == 2; }
    bool spawnsFlyers() const { return kind == 3 || kind == 5; }
    float swoopLen() const { return kind == 3 ? 0.45f : (kind == 4 ? 1.1f : 0.75f); }
};
