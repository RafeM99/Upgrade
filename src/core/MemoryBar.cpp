// Owner: Systems (Camilla's Claude).
#include "core/MemoryBar.h"
#include "core/Config.h"

void MemoryBar::init() {
    value = 0.0f;
    maxValue = Config::getFloat("memory", "complete_at", 100.0f);
    loseAmount = Config::getFloat("memory", "lose_amount", 10.0f);
}

void MemoryBar::add(float amount) {
    value += amount;
    if (value > maxValue) value = maxValue;
}

void MemoryBar::loseMemory(float floorValue) {
    value -= loseAmount;
    if (value < floorValue) value = floorValue;
}
