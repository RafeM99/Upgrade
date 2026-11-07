#pragma once
// Owner: Audio, level design and UI (area 4). Starter code from the skeleton: replace freely.
// Memory Bar, player indicators, era transition popup, placeholder ending.
// To do (UI): era styles for the bar, title screen, pause menu, ending scene and credits.

class Game;

class Hud {
public:
    void init();
    void draw(const Game& game);
};
