// Hud.h: Memory Bar, player indicators, banners, title, pause and ending screens.
// Each graphics stage draws its own flat HUD, in that era's style (see Hud.cpp).
#pragma once

class Game;

class Hud {
public:
    void drawPlaying(Game& g);
    void drawTitle(Game& g);
    void drawPause(Game& g);
    void drawSettings(Game& g);   // akshat: display / graphics / audio options
    void drawEnding(Game& g);
};
