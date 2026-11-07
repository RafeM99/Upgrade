// Story.h: the story of the game, told in its own UI (Camilla's Claude, claude-camilla).
//
// Gran (Edie, 80) has Alzheimer's. Her grandson Sam walks back through her memories with her,
// newest first, oldest last. Every memory she gets back, she sees more clearly: that is the
// graphics upgrade between stages. Each memory has its own boss (see Boss::kind in Enemy.h).
//
// Three pieces, all separate from the level HUD:
//   * memory cards: full screen, between levels. A photograph of the memory (taken from the
//     game the moment the card opens), its title and year, a few lines from Gran and Sam,
//     and a timeline that moves further back each time.
//   * cinematic bars: black bars top and bottom during play, for a boss arriving and a
//     memory coming back.
//   * the ending: the album of everything she remembered, then the present day.
// All the words are in assets/strings.txt (keys story_*, memory_*, boss_*, end_*).
#pragma once
#include "raylib.h"
#include "render/ArtSpec.h"

#include <string>
#include <vector>

class Game;

class Story {
public:
    static constexpr int PROLOGUE = -1;

    void unload();

    // ---- memory cards
    void open(const std::vector<int>& cards);   // card ids: PROLOGUE or a memory 0..6
    bool active() const { return cur_ >= 0 && cur_ < (int)queue_.size(); }
    // pressed: a button went down this frame. autoplay: move on by itself.
    void update(float dt, bool pressed, bool autoplay);
    void drawCard(Game& g);

    // A photograph of memory `era`, taken from the screen (cropped to the game image).
    void setPhoto(int era, Image screenCrop);

    // ---- cinematic bars during play
    void showBars(const std::string& label, const std::string& title, const std::string& line, float seconds);
    void updateBars(float dt) { barsT_ = barsT_ > dt ? barsT_ - dt : 0; }
    void clearBars() { barsT_ = 0; }
    void drawBars(Game& g);

    // ---- the ending
    void startEnding();
    bool updateEnding(float dt, bool pressed, bool autoplay);   // true: back to the title
    void drawEnding(Game& g);

private:
    struct Line { int who; std::string text; };   // who: -1 narration, 0 Gran, 1 Sam
    struct Card {
        int id = PROLOGUE;
        std::string label, title, when;
        std::vector<Line> lines;
        float t = 0;
        int totalChars = 0;
    };
    std::vector<int> queue_;
    int cur_ = -1;
    Card card_;
    Texture2D photo_[art::ERA_COUNT]{};

    std::string barsLabel_, barsTitle_, barsLine_;
    float barsT_ = 0, barsLen_ = 0;

    int endPhase_ = 0;      // 0 album, 1 present day, 2 closing words and credits
    float endT_ = 0;

    void load(int id);
    void loadEnding();
    float shownChars() const;
    bool typed() const { return shownChars() >= card_.totalChars; }
    void drawLines(Game& g, float x, float y, float w, float alpha);
    void drawPolaroid(Game& g, int era, float cx, float cy, float w, float angle, float alpha, const std::string& caption);
    void drawTimeline(Game& g, int current, int restoredUpTo, float alpha);
};
