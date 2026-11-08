// MemoryScene.h (akshat): the memory cutscenes. Each story card is a short animated scene of the
// memory, acted out in the art style of its stage: Gran and Sam from the stage's own sprite sheets,
// everyone and everything else drawn here from primitives. Pixel stages draw into a small canvas
// that is quantised to the stage's colours (1-bit dither, four greys, the 16-colour palette...)
// and scaled up crisply; the 2.5D stage is a smooth parallax painting; the 3D stage and the
// ending are small lit 3D dioramas. Story (ui/Story.cpp) draws the subtitles, captions and
// timeline on top.
//
// Time: t is seconds since the scene opened; lineStart[i] is when subtitle line i starts and
// end is when the last line is over. Scenes pace their action on the lines, so a voiced line
// and the thing it talks about happen together, and a skipped line skips the action with it.
#pragma once
#include "raylib.h"

#include <vector>

class Game;

namespace scene {

constexpr int PROLOGUE = -1;   // the present day, at the start
constexpr int ENDING = 7;      // the present day, at the end

// The art stage a scene is drawn in.
int styleOf(int id);
// Draw scene `id` filling the window rectangle dst.
void draw(Game& g, int id, float t, const std::vector<float>& lineStart, float end, Rectangle dst);
void unload();

}  // namespace scene
