// Art.h: loads the generated sprite sheets for all four eras.
#pragma once
#include "raylib.h"
#include "render/ArtSpec.h"

struct EraArt {
    Texture2D player[2]{}, blob{}, flyer{}, brute{}, boss{}, tiles{}, wall{};
    Texture2D bossKind[2]{};   // akshat: Alarm Clock, Hourglass
    Texture2D bg[art::BG_LAYERS]{};
};

struct Art {
    EraArt era[art::ERA_COUNT];
    Texture2D ending[art::ENDING_COUNT]{};
    void load();
    void unload();
};

// Source rectangle of frame `f` in a one-row sheet of frames fw x fh tiles.
Rectangle frameRect(const Texture2D& sheet, int f, float fw, float fh, int era);
