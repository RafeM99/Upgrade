#include "core/Game.h"

#include "raylib.h"

void Game::init()
{
    player.spawn(Vector2{200.0f, static_cast<float>(level.floorTop())});
}

void Game::update(float dt)
{
    player.update(dt, level);
}

void Game::draw()
{
    ClearBackground(Color{20, 20, 30, 255});
    level.draw();
    player.draw();
    DrawText("Upgrade", 10, 10, 20, RAYWHITE);
}
