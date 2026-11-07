#pragma once
// Five console eras. The era goes up by one each time a world's flag is reached.
// 0 = NES, 1 = SNES, 2 = N64, 3 = GameCube, 4 = Wii.

constexpr int kEraCount = 5;

class EraManager {
public:
    void init() { era = 0; }
    int current() const { return era; }
    bool isLast() const { return era == kEraCount - 1; }
    void setEra(int e);   // fires events().eraChanged

private:
    int era = 0;
};
