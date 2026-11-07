// Owner: Rafale (core player). Starter from the skeleton: fill in movement here.

#include "player/Player.h"

#include "level/Level.h"

void Player::spawn(Vector2 at)
{
    pos = at;
    vel = Vector2{0.0f, 0.0f};
    onGround = false;
}

void Player::update(float dt, const Level& level)
{
    (void)dt;
    // Rafale: run with A/D, jump with Space, gravity and landing go here.
    pos.y = static_cast<float>(level.floorTop());
    onGround = true;
}

void Player::draw() const
{
    DrawRectangle(static_cast<int>(pos.x - WIDTH / 2.0f), static_cast<int>(pos.y - HEIGHT),
                  static_cast<int>(WIDTH), static_cast<int>(HEIGHT), ORANGE);
}
