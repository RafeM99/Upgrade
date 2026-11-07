#pragma once
// Title screen, top bar (score, coins, world, deaths, time), flag progress, death taunts,
// the era-upgrade and world-clear popups. Every era has its own HUD style.

class Game;

class Hud {
public:
    void init();
    void draw(const Game& game);
};
