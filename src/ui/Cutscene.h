// Cutscene.h (akshat): Pokemon-style story scenes. A dialogue box with a typewriter and
// voice blips, big pixel portraits of whoever is talking, and a wild-battle style
// "VS" intro for the bosses. Scenes are written in assets/cutscenes.txt.
// While a scene is active the Game stops updating the world (it keeps drawing it).
#pragma once
#include "raylib.h"

#include <deque>
#include <functional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

class Game;

class Cutscene {
public:
    void load(const std::string& path);
    void resetRun() { played_.clear(); }   // every scene may play again (new run)

    // Queue a scene. Each plays once per run. Returns false when it will not play
    // (unknown, already played, or cutscenes are off in an automated test run).
    bool play(Game& g, const std::string& name);
    // Run fn once nothing is playing any more (right now if nothing is).
    void then(std::function<void()> fn);

    bool active() const { return cur_ != nullptr; }
    void update(Game& g, float dt, bool firstTick);
    void draw(Game& g);

private:
    struct Line { std::string speaker, text; };
    struct Scene { std::string name, vs; std::vector<Line> lines; };
    enum Phase { PH_FLASH, PH_TALK, PH_OUTRO };

    std::unordered_map<std::string, Scene> scenes_;
    std::set<std::string> played_;
    std::deque<const Scene*> queue_;
    std::vector<std::function<void()>> then_;

    const Scene* cur_ = nullptr;
    Phase phase_ = PH_TALK;
    float t_ = 0;                 // seconds since this scene began
    float phaseT_ = 0;            // seconds in this phase
    int line_ = 0, page_ = 0;
    std::vector<std::vector<std::string>> pages_;   // pages of wrapped lines for the current line
    float shown_ = 0;             // characters revealed on this page
    float doneT_ = 0;             // seconds since the page finished typing
    float lineT_ = 0;             // seconds since the line began (portrait bob)
    float holdT_ = 0;             // hold-to-skip progress
    bool armed_ = false;          // a press happened inside this scene (so a hold counts)
    int blips_ = 0;
    float shake_ = 0;
    bool slammed_ = false;
    int ticks_ = 0;
    std::string rightWho_;        // who stands in the right portrait slot
    float rightIn_ = 0, leftIn_ = 0;
    float stashBanner_ = 0;
    Texture2D oneBit_[4]{};       // 1-bit portraits made from the 8-bit sheets (gran, sam, alarm, hourglass)

    void begin(Game& g, const Scene* s);
    void finish(Game& g);
    void startLine(Game& g);
    int pageChars() const;
    void advance(Game& g);
};
