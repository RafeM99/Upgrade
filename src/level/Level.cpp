#include "level/Level.h"

#include "raylib.h"

namespace {
const int FLOOR_TOP = 640;
}

int Level::floorTop() const
{
    return FLOOR_TOP;
}

void Level::draw() const
{
    // Flat floor: rows of 32x32 placeholder blocks from the floor top to the bottom of the screen
    const int width = GetScreenWidth();
    const int height = GetScreenHeight();
    for (int y = FLOOR_TOP; y < height; y += TILE) {
        for (int x = 0; x < width; x += TILE) {
            DrawRectangle(x, y, TILE, TILE, Color{200, 170, 130, 255});
            DrawRectangleLines(x, y, TILE, TILE, Color{120, 90, 60, 255});
        }
    }
}
