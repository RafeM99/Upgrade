#pragma once
// A small plumber-ish hero: walk, run (hold attack), variable jump, stomp, head-bonk blocks.
// Dies on spikes, enemies and pits; Game respawns them.
#include "player/Input.h"
#include "raylib.h"

class Level;

class Player {
public:
    int index = 0;
    bool active = false;   // P2 joins by pressing any of their buttons
    bool god = false;      // test mode: never dies
    Vector2 pos{0, 0};     // bottom-left corner of the box, y points up
    Vector2 vel{0, 0};
    bool onGround = false;
    int facing = 1;
    bool running = false;

    bool dead = false;
    bool justDied = false;     // Game reads and clears this to count deaths
    float deadTimer = 0.0f;    // respawn when this runs out
    float invuln = 0.0f;       // blink after respawning
    Vector2 deathPos{0, 0};

    bool bumped = false;       // hit a block from below this step
    int bumpX = 0, bumpY = 0;

    static constexpr float W = 0.8f;
    static constexpr float H = 0.95f;

    void spawn(Vector2 at);
    void update(float dt, const InputState& in, Level& level);
    void kill(bool force = false);   // force: even while blinking (pits)
    void bounce();             // after a stomp; higher if jump is held
    Rectangle box() const { return {pos.x, pos.y, W, H}; }
    Vector2 centre() const { return {pos.x + W * 0.5f, pos.y + H * 0.5f}; }
    Vector2 safePos() const { return lastSafe; }

private:
    Vector2 lastSafe{0, 0};
    float coyote = 0.0f;
    float jumpBuffer = 0.0f;
    bool jumpHeld = false;
};
