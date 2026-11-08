// Enemy.h: the creatures of the Forgetting (brief section 8) and the era mini-boss.
//   Static Blob  - walks back and forth, turns at walls and ledges. Dies to a stomp or attack.
//   Glitch Flyer - floats in a sine wave and drifts toward the nearest player; glitches (blinks) from 16-bit on.
//   Fog Brute    - 3 hits, from the 8-bit era on. Walks; from 16-bit on it winds up and charges.
//   The bosses (stages 2, 4, 6: the School Bell, the Last Train, the Forgetting) appear when the
//                  Memory Bar is full. They throw time-themed shots you can DEFLECT with ATTACK, telegraph
//                  a big attack, and open a WEAK POINT when dizzy or stuck: hit it with ATTACK (both
//                  players at once: "Remember together"). See Boss below and core/GameBoss.cpp.
#pragma once
#include <algorithm>
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

// ------------------------------------------------------------------ boss fight (akshat, boss rework)
// A box in world tiles (x right, y down), top-left corner.
struct HitBox {
    float x = 0, y = 0, w = 0, h = 0;
    float cx() const { return x + w / 2; }
    float cy() const { return y + h / 2; }
    bool overlaps(const HitBox& o) const { return x < o.x + o.w && x + w > o.x && y < o.y + o.h && y + h > o.y; }
};

// What a boss throws. Every shot can be DEFLECTED: attack it while it is in front of you and it
// flies back at the boss; a deflected shot that hits the boss knocks it dizzy (the weak point opens).
enum BossShotType {
    SHOT_RING_GROUND,   // the School Bell: a ring of sound rolling along the floor (jump it or deflect it)
    SHOT_RING_AIR,      // the School Bell: a ring of sound flying at you
    SHOT_COAL,          // the Last Train: a lump of clock-coal, lobbed in an arc
    SHOT_STEAM,         // the Last Train: a puff of steam drifting along the line
    SHOT_ORB,           // the Forgetting: a "lost moment", a slow orb that drifts after you
    SHOT_TYPES
};

struct BossShot {
    int type = SHOT_ORB;
    float x = 0, y = 0;        // centre
    float vx = 0, vy = 0;
    float r = 0.4f;            // half the side of its box
    float t = 0;
    bool reflected = false;    // deflected: now flies at the boss and cannot hurt the players
    bool dead = false;
    int by = -1;               // who deflected it
    int id = 0;                // a number for each shot (the bots use it to decide which to deflect)
    HitBox box() const { return {x - r, y - r, 2 * r, 2 * r}; }
};

// How hard the fight is, per difficulty (the table is FIGHT_TUNING in enemies/BossPowers.h).
struct FightTuning {
    float shotEvery;     // seconds between two shots while the boss hovers
    float shotSpeed;     // multiplies every shot's speed
    float hoverTime;     // seconds of hovering (and shooting) before its big attack
    float aimTime;       // the telegraph before the big attack
    float stunDeflect;   // how long a deflected shot keeps it dizzy (weak point open)
    float stunMiss;      // how long it is stuck after its big attack misses
    int stunHits;        // weak-point hits before it shakes itself free
    float trainSpeed;    // the Last Train's charge, tiles per second
    float minionEvery;   // seconds between minions (0: none)
    float hpMul;         // multiplies [boss] hp_<stage>
    bool hoverHurts;     // touching the boss outside its attacks hurts too (else it pushes you away)
};

enum BossState { BS_ENTER = 0, BS_HOVER = 1, BS_ATTACK = 2, BS_STUN = 3, BS_RISE = 4, BS_DYING = 5, BS_AIM = 6, BS_RECOVER = 7 };

struct Boss {
    bool active = false;
    float x = 0, y = 0;        // centre (where the sprite / model is drawn)
    float w = 2.4f, h = 2.2f;  // rough overall size, for the camera and shadows (hit boxes: bodyBoxes)
    int hp = 3, maxHp = 3;
    // 0 enter, 1 hover (shoots), 6 aim (telegraph), 2 attack (swoop / charge / slam), 3 STUNNED (weak
    // point open), 4 rise, 5 dying, 7 recover (after an attack that hit someone: no opening)
    int state = 0;
    float t = 0, stateTime = 0, hurt = 0, spawnTimer = 0;
    float fromX = 0, fromY = 0, toX = 0, toY = 0;
    bool wantsSpawn = false;   // the Game spawns a minion when this is set
    bool defeated = false;
    int kind = 6;              // which memory's boss this is (the stage it appears in)
    bool landed = false;       // set for one update when an attack ends on the ground
    bool charge = false;       // the train: this attack is a charge along the floor

    // ---- the fight (akshat, boss rework)
    FightTuning ft{};
    float floorY = 12;         // top of the arena floor
    float arenaL = 0, arenaR = 20;
    int phase = 1;             // 2 once it is down to half its health: faster, and the Forgetting slams twice
    float shotTimer = 2;
    int shotCount = 0;
    std::vector<BossShot> shots;
    float stunT = 0, stunLen = 1;
    int stunHits = 0;
    bool coopDone = false;     // "Remember together" lands once per opening
    bool retaliate = false;    // knocked dizzy by its own shot: it answers with its big attack straight after
    int side = 1;              // the train: which end of the arena it stands at (-1 left, 1 right)
    int face = -1;             // which way it looks (the Forgetting's eye, the bell's tilt)
    int slamsLeft = 0;         // the Forgetting: slams still to come in this attack (phase 2: two)
    bool hitSomeone = false;   // its attack hurt a player (set by the Game): no opening this time
    bool held = false;         // a power is being announced: it waits and does not shoot (set by the Game)
    float tilt = 0;            // radians: the bell tips over when dizzy
    float deflectT = 0, deflectX = 0, deflectY = 0;   // the last parry flash
    bool phase2Told = false;
    float eyeDX = 0, eyeDY = 0;   // the Forgetting: where its eye is drawn (set by Models3D in full 3D)
    bool eyeSet = false;   // the Game announced phase 2
    bool wantsBlast = false;   // the train lets off steam as it recovers: players near it are pushed back
    bool wantsStunSound = false, wantsShotSound = false, wantsWhistle = false, wantsSlam = false;

    void start(float camX, int era, int hpTotal, int kind, const FightTuning& tuning, float floorY, float arenaL, float arenaR);
    void update(float dt, float camLeft, float camRight, const Level& level, const Player* players, int era, float timeScale);
    void stun(float seconds);   // knocked dizzy: the weak point opens
    bool vulnerable() const { return state == BS_STUN && stateTime > 0.25f; }   // the weak point is open
    bool stunned() const { return state == BS_STUN; }
    float stunLeft() const { return state == BS_STUN ? std::max(0.0f, stunT) / std::max(0.01f, stunLen) : 0.0f; }
    bool dangerous() const { return state == BS_ATTACK || (ft.hoverHurts && (state == BS_HOVER || state == BS_AIM || state == BS_RISE)); }
    bool aiming() const { return state == BS_AIM || state == BS_ATTACK; }
    bool spawnsFlyers() const { return kind == 3 || kind == 5; }
    float swoopLen() const { return kind == 3 ? 0.45f : (kind == 6 ? 0.55f : 0.75f); }
    // Hit boxes, matched to each boss's art (see Renderer / Models3D): the body hurts on contact
    // during attacks; the weak point takes damage only while the boss is stunned.
    int bodyBoxes(HitBox out[3]) const;
    HitBox weakBox() const;
    HitBox bounds() const;     // all body boxes together
    HitBox slamZone() const;   // the Forgetting: where its tentacles hit the floor (empty when none)
    float hoverY() const;      // where it hovers / where it stands
    float groundY() const;     // its centre when it is down on the floor
};
