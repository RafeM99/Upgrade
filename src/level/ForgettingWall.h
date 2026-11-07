#pragma once
// Owner: Systems (Camilla's Claude).
// The wall of static that advances from the left. Speeds up each era and catches up
// faster when the players are far ahead. Off until [wall] enabled = 1 in config.ini.
// To do (systems, 18:30 milestone): touching the wall fires events().playerFaded.

class ForgettingWall {
public:
    bool enabled = false;
    float x = -12.0f;     // world x of the wall's right edge

    void init();
    void update(float dt, int era, float playersMinX);
    float speedNow() const { return speed; }

private:
    float speed = 0.0f;
};
