// BossPowers.h: each memory's boss has its own power over time (Camilla's Claude, claude-camilla).
//
//   0 The 3 a.m. Clock   TIME STOP      winds up, then freezes the players in place for a moment
//   1 The Empty Shirt    REWIND         if you don't hit it again soon, it rewinds and takes back a hit
//   2 The School Bell    HURRY          rings, and the whole world runs fast for a few seconds
//   3 The Storm          HEAVY RAIN     time drags for the players in the rain (they move slowly)
//   4 The Last Train     ECHOES         every charge is followed by echoes of itself on the same line
//   5 The Scarecrow      HARVEST RUSH   the day races by: it calls crows and they fly fast
//   6 The Forgetting     PULL BACK      pulls the players back to where they were a few seconds ago
//
// Every active power is announced first: the boss stops and pulses, and the HUD names it.
// How strong they are depends on the difficulty (title screen, --difficulty, [game] difficulty).
#pragma once
#include "raylib.h"

#include <string>
#include <vector>

enum Difficulty { DIFF_GENTLE, DIFF_MEDIUM, DIFF_HARD, DIFF_COUNT };

struct PowerTuning {
    float every;            // seconds of hovering between two powers
    float warn;             // the warning before a power lands
    float freeze;           // clock: how long the players are frozen
    bool attackWhileFrozen; // clock: may it swoop while they are frozen?
    int heals;              // shirt: how many hits it may take back in a fight
    float healWindow;       // shirt: hit it again within this many seconds or it rewinds
    float hurryK, hurryT;   // bell: world speed and how long
    float rainK, rainT;     // storm: player speed in the rain and how long
    int echoes;             // train: echoes after each charge
    bool echoHurts;         // train: do echoes hurt (or only push you away)?
    float rushK, rushT;     // scarecrow: crow speed and how long
    int crows;              // scarecrow: crows it calls each time
    float pullBack;         // forgetting: seconds the players are pulled back
    int extraHits;          // every boss needs this many more hits
};

// Gentle: clearly announced, short, more annoying than dangerous. Medium: they can hurt you if
// ignored. Hard: a real threat, more often, and every boss takes one more hit.
constexpr PowerTuning POWER_TUNING[DIFF_COUNT] = {
    // every warn  freeze atk   heals win   hurry      rain        echo hurt   rush       crows pull extra
    {9.0f, 1.3f, 0.9f, false, 1, 3.0f, 1.25f, 3.0f, 0.75f, 3.0f, 1, false, 1.4f, 4.0f, 1, 2.0f, 0},
    {7.0f, 1.0f, 1.3f, true,  2, 2.5f, 1.40f, 4.0f, 0.65f, 4.0f, 1, true,  1.7f, 5.0f, 2, 3.0f, 0},
    {5.5f, 0.8f, 1.8f, true,  4, 2.0f, 1.60f, 5.0f, 0.55f, 5.0f, 2, true,  2.0f, 6.0f, 3, 4.0f, 1},
};

// One echo of the Last Train: a replay of its last charge, a little later.
struct TrainEcho {
    std::vector<Vector2> path;   // boss centre, one sample per update
    float t = 0, delay = 0;
    bool active() const { return t >= delay && (size_t)((t - delay) * 60.0f) < path.size(); }
    Vector2 pos() const { size_t i = (size_t)((t - delay) * 60.0f); return path[i < path.size() ? i : path.size() - 1]; }
};

// What the powers are doing right now (owned by the Game, read by the renderer and HUD).
struct BossPowerState {
    float timer = 0;            // counts down while the boss hovers; at 0 it starts a warning
    float warnT = 0;            // > 0 while a power is being announced
    float freezeT = 0;          // clock
    float hurryT = 0, hurryK = 1;
    float rainT = 0, rainK = 1;
    float rushT = 0, rushK = 1;
    float pullT = 0;            // forgetting: the pull-back animation
    int healsLeft = 0;
    int lastHp = 0;
    float sinceHit = -1;        // shirt: seconds since its last hit (-1: nothing to take back)
    std::vector<TrainEcho> echoes;
    std::vector<Vector2> curPath;   // the train's current charge, being recorded
    std::string label;          // the HUD line for the power, and how long it shows
    float labelT = 0;
    bool warning() const { return warnT > 0; }
    void reset() { *this = BossPowerState(); }
};

const char* difficultyId(int d);   // "gentle", "medium", "hard"
int difficultyFromString(const std::string& s, int fallback);
