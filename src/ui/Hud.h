// Hud.h: Memory Bar, player indicators, banners, title, pause and ending screens.
// The bar and panels restyle for each era (pixel bar -> glossy bar).
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
