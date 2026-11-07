// Owner: Systems (Camilla's Claude).
#include "level/ForgettingWall.h"
#include <string>
#include "core/Config.h"

void ForgettingWall::init() {
    enabled = Config::getBool("wall", "enabled", false);
    x = Config::getFloat("wall", "start_x", -12.0f);
    speed = 0.0f;
}

void ForgettingWall::update(float dt, int era, float playersMinX) {
    if (!enabled) return;
    speed = Config::getFloat("wall", "speed_era" + std::to_string(era), 1.0f);
    if (playersMinX - x > Config::getFloat("wall", "catchup_distance", 25.0f))
        speed *= Config::getFloat("wall", "catchup_multiplier", 2.0f);
    x += speed * dt;
}
