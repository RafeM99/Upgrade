#pragma once
// Enemies, coins, flagpoles and score popups. Spawned from letters in chunk files.
//   S walker: walks, turns at walls, walks off ledges. Stomp it.
//   G flyer: bobs up and down and drifts left.
//   B brute: big and slow, takes two stomps.
//   Q coin mimic: looks exactly like a coin until you get close. Then it bites.
//   P flagpole: reach it to clear the world. From the SNES era on, it runs away first.
#include <string>
#include <vector>
#include "raylib.h"

class Game;

struct Enemy {
    char type;             // 'S', 'G', 'B', 'Q'
    Vector2 pos;           // centre
    Vector2 home;          // spawn point, for the flyer's bob
    Vector2 vel{0, 0};
    int dir = -1;
    int hp = 1;
    bool alive = true;
    bool awake = false;
    bool stomped = false;  // squashed, about to vanish
    float deathTimer = 0.0f;
    float flash = 0.0f;
    float t = 0.0f;
};

struct Coin {
    Vector2 pos;           // centre
    bool taken;
};

struct Flag {
    float x;               // pole centre
    float baseY;           // ground under the pole
    float targetX;         // where it is running to
    int runs = 0;          // times it has run away
    bool taken = false;
    float dropT = 0.0f;    // flag sliding down after the touch, 0..1
};

struct Popup {
    Vector2 pos;
    float t;
    std::string text;      // empty: a coin popping out of a block
};

class EnemyManager {
public:
    std::vector<Enemy> enemies;
    std::vector<Coin> coins;
    std::vector<Flag> flags;
    std::vector<Popup> popups;

    void clear();
    void update(float dt, Game& game);
    void spawn(char type, Vector2 bottomCentre, bool awake);
    void bumpKill(int tileX, int tileTopY, Game& game);   // enemies standing on a bonked block
    void coinPop(Vector2 at) { popups.push_back({at, 0.0f, ""}); }
    void popText(Vector2 at, const std::string& text) { popups.push_back({at, 0.0f, text}); }

    static Vector2 halfSize(const Enemy& e);
    static Rectangle box(const Enemy& e);
};
