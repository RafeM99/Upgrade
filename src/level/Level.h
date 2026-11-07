#pragma once

// World units are pixels, y points down.
constexpr int TILE = 32;

class Level {
public:
    // Top edge of the floor, in pixels.
    int floorTop() const;
    void draw() const;
};
