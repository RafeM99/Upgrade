#pragma once
// Owner: Systems (Camilla's Claude).
// The shared Memory Bar, 0 to 100. Both players' fragments fill it.

class MemoryBar {
public:
    float value = 0.0f;
    float maxValue = 100.0f;

    void init();
    void add(float amount);
    void loseMemory(float floorValue);   // both Faded: drop by lose_amount, never below the era's start

private:
    float loseAmount = 10.0f;
};
