#pragma once
// Owner: Core player (area 2). Starter code from the skeleton: replace freely.
// Placeholder run and jump with tile collision, enough for the 14:00 milestone.
// To do (core player): stomp, attack, abilities, Faded and revive, pull back to partner.
#include <set>
#include <string>
#include "player/Input.h"
#include "raylib.h"

class Level;

class Player {
public:
    int index = 0;
    bool active = false;   // P2 joins by pressing any of their buttons
    bool faded = false;
    Vector2 pos{0, 0};     // bottom-left corner of the box, y points up
    Vector2 vel{0, 0};
    bool onGround = false;
    int facing = 1;
    std::set<std::string> abilities;   // Game adds names here when an era is reached

    static constexpr float W = 0.8f;
    static constexpr float H = 0.95f;

    void spawn(Vector2 at);
    void update(float dt, const InputState& in, const Level& level);
    Rectangle box() const { return {pos.x, pos.y, W, H}; }
    Vector2 centre() const { return {pos.x + W * 0.5f, pos.y + H * 0.5f}; }
    bool has(const std::string& ability) const { return abilities.count(ability) > 0; }

private:
    Vector2 lastSafe{0, 0};
    float coyote = 0.0f;
    float jumpBuffer = 0.0f;
};
