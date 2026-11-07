#include "render/Art.h"
#include "core/Paths.h"

#include <string>

namespace {
Texture2D loadTex(const std::string& rel, int era, const std::string& fallbackRel = "")
{
    std::string p = assetPath(rel);
    // A later era with no art of its own yet borrows the era before it (era 4 -> era 3).
    if (!fallbackRel.empty() && !FileExists(p.c_str())) p = assetPath(fallbackRel);
    Texture2D t = LoadTexture(p.c_str());
    if (t.id == 0) {
        // Missing art never stops the game: use a magenta square instead.
        Image img = GenImageColor(16, 16, MAGENTA);
        t = LoadTextureFromImage(img);
        UnloadImage(img);
    }
    if (art::isHiRes(era)) {
        GenTextureMipmaps(&t);
        SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
    } else {
        SetTextureFilter(t, TEXTURE_FILTER_POINT);
    }
    SetTextureWrap(t, TEXTURE_WRAP_REPEAT);
    return t;
}
}  // namespace

void Art::load()
{
    for (int e = 0; e < art::ERA_COUNT; ++e) {
        std::string d = "art/era" + std::to_string(e) + "/";
        // era 4 shares era 3's sheet layouts, so era 3 art stands in for any missing era 4 file
        std::string fb = e == art::ERA_MODERN ? "art/era" + std::to_string(e - 1) + "/" : "";
        auto ld = [&](const std::string& f) { return loadTex(d + f, e, fb.empty() ? "" : fb + f); };
        EraArt& a = era[e];
        a.player[0] = ld("player_p1.png");
        a.player[1] = ld("player_p2.png");
        a.blob = ld("enemy_blob.png");
        a.flyer = ld("enemy_flyer.png");
        a.brute = ld("enemy_brute.png");
        a.boss = ld("enemy_boss.png");
        a.tiles = ld("tiles.png");
        a.wall = ld("wall.png");
        for (int l = 0; l < art::BG_LAYERS; ++l) a.bg[l] = ld("bg_" + std::to_string(l) + ".png");
    }
    for (int i = 0; i < art::ENDING_COUNT; ++i) {
        ending[i] = loadTex("art/ending_" + std::to_string(i) + ".png", 3);
        SetTextureWrap(ending[i], TEXTURE_WRAP_CLAMP);
    }
}

void Art::unload()
{
    for (auto& a : era) {
        UnloadTexture(a.player[0]); UnloadTexture(a.player[1]);
        UnloadTexture(a.blob); UnloadTexture(a.flyer); UnloadTexture(a.brute); UnloadTexture(a.boss);
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
