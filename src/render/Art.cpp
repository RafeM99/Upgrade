#include "render/Art.h"
#include "core/Paths.h"

#include <string>

namespace {
Texture2D loadTex(const std::string& rel, int era)
{
    std::string p = assetPath(rel);
    Texture2D t = LoadTexture(p.c_str());
    if (t.id == 0) {
        // Missing art never stops the game: use a magenta square instead.
        Image img = GenImageColor(16, 16, MAGENTA);
        t = LoadTextureFromImage(img);
        UnloadImage(img);
    }
    // every stage is pixel art, so every texture keeps hard pixels (no smoothing)
    SetTextureFilter(t, TEXTURE_FILTER_POINT);
    SetTextureWrap(t, TEXTURE_WRAP_REPEAT);
    return t;
}
}  // namespace

void Art::load()
{
    for (int e = 0; e < art::ERA_COUNT; ++e) {
        std::string d = "art/era" + std::to_string(e) + "/";
        EraArt& a = era[e];
        a.player[0] = loadTex(d + "player_p1.png", e);
        a.player[1] = loadTex(d + "player_p2.png", e);
        a.blob = loadTex(d + "enemy_blob.png", e);
        a.flyer = loadTex(d + "enemy_flyer.png", e);
        a.brute = loadTex(d + "enemy_brute.png", e);
        a.boss = loadTex(d + "enemy_boss.png", e);
        if (FileExists(assetPath(d + "boss_story.png").c_str())) a.storyBoss = loadTex(d + "boss_story.png", e);
        a.tiles = loadTex(d + "tiles.png", e);
        a.wall = loadTex(d + "wall.png", e);
        for (int l = 0; l < art::BG_LAYERS; ++l) a.bg[l] = loadTex(d + "bg_" + std::to_string(l) + ".png", e);
    }
    for (int i = 0; i < art::ENDING_COUNT; ++i) {
        ending[i] = loadTex("art/ending_" + std::to_string(i) + ".png", art::LAST_ERA);
        SetTextureWrap(ending[i], TEXTURE_WRAP_CLAMP);
    }
}

void Art::unload()
{
    for (auto& a : era) {
        UnloadTexture(a.player[0]); UnloadTexture(a.player[1]);
        UnloadTexture(a.blob); UnloadTexture(a.flyer); UnloadTexture(a.brute); UnloadTexture(a.boss);
        if (a.storyBoss.id) UnloadTexture(a.storyBoss);
        UnloadTexture(a.tiles); UnloadTexture(a.wall);
        for (auto& b : a.bg) UnloadTexture(b);
    }
    for (auto& t : ending) UnloadTexture(t);
}

Rectangle frameRect(const Texture2D& sheet, int f, float fw, float fh, int era)
{
    float pw = fw * art::TILE_PX[era], ph = fh * art::TILE_PX[era];
    (void)sheet;
    return Rectangle{f * pw, 0, pw, ph};
}
