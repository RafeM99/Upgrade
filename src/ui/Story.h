// Story.h: the story of the game, told in its own UI (Camilla's Claude, claude-camilla).
//
// Gran (Edie, 80) has Alzheimer's. Her grandson Sam walks back through her memories with her,
// newest first, oldest last. Every memory she gets back, she sees more clearly: that is the
// graphics upgrade between stages. Each memory has its own boss (see Boss::kind in Enemy.h).
//
// Three pieces, all separate from the level HUD:
//   * memory scenes: full screen, between levels. The memory acted out as a short animated
//     scene in its stage's art style (ui/MemoryScene), with the lines from Gran and Sam as
//     voiced subtitles, the title and year as an opening caption, and a timeline strip that
//     moves further back each time.
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
    // who: -1 narration, 0 Gran, 1 Sam. text: as shown (speech in quotes).
    struct Line { int who; std::string text; float start = 0, dur = 0; };
    struct Card {
        int id = PROLOGUE;   // PROLOGUE, a memory 0..6, or scene::ENDING
        std::string label, title, when;
        std::vector<Line> lines;
        std::vector<float> starts;   // when each line starts (for the scene's choreography)
        float t = 0;
        float end = 0;               // when the last line is over
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
    void loadLines(const std::string& prefix);   // akshat: the lines, timed to their voiceover
    int voicedLine_ = -1;    // akshat: the card line whose voiceover is playing
    void voiceTick();        // akshat: start the voiceover of the line being spoken now
    void loadEnding();
    int lineAt() const;      // akshat: the line on screen now (-1 before the first)
    bool typed() const { return card_.t >= card_.end; }
    bool press();            // akshat: a button: finish the line, next line, or true: the card is done
    void drawScene(Game& g, int restoredUpTo);   // akshat: the memory scene, subtitles, caption, timeline
    void drawSubtitle(float alpha);
    void drawCaption(float alpha);
    void drawPolaroid(Game& g, int era, float cx, float cy, float w, float angle, float alpha, const std::string& caption);
    void drawTimelineStrip(int current, int restoredUpTo, float alpha);
};
