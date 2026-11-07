#pragma once

#include "level/Level.h"
#include "player/Player.h"

class Game {
public:
    void init();
    void update(float dt);
    void draw();

private:
    Level level;
    Player player;
};
