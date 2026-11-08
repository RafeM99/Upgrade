#include "audio/Voice.h"
#include "ui/Story.h"
#include "core/Game.h"
#include "core/Paths.h"
#include "core/Strings.h"
#include "rlgl.h"
#include "ui/MemoryScene.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <map>

// Each memory is a short animated scene in its stage's own art style (ui/MemoryScene): the
// memories get clearer, the scenes get clearer. Story frames it like a film: letterbox bars,
// the title and year as an opening caption, the lines as voiced subtitles in the bottom bar and
// a small timeline in the top bar. The ending's album still uses photographs taken in play. Everything is laid out on a
// 1280 x 720 page and scaled to the window (cards, ending) or the game image (the bars).
// Text goes through ui/Theme: Inter for what people say, Pixelify Sans for labels and years.

namespace {

float S = 1, OX = 0, OY = 0, WW = 1280, WH = 720;   // page scale, offset and size in window pixels

const Color INK = {21, 17, 14, 255};
const Color PAPER = {241, 231, 211, 255};
const Color NARR = {203, 188, 165, 255};
const Color DIM = {128, 114, 98, 255};
const Color AMBER = {232, 165, 75, 255};
const Color LILAC = {205, 176, 255, 255};
const Color PHOTO_INK = {58, 51, 44, 255};
const Color WHO_COL[2] = {{242, 138, 30, 255}, {60, 156, 240, 255}};   // Gran, Sam: the players' colours

constexpr float CPS = 42.0f;        // subtitle characters typed per second
constexpr float FIRST_LINE = 2.8f;  // the opening caption shows before the first line
constexpr float BAR_TOP = 64, BAR_BOT = 118;   // letterbox bars, page units
constexpr int MEMORIES = art::ERA_COUNT;

Color fade(Color c, float a) { c.a = (unsigned char)(c.a * std::max(0.0f, std::min(1.0f, a))); return c; }

// card: the whole window (16:9 fit, the room fills the rest); otherwise the game image
void page(Game& g, bool card = true)
{
    Rectangle r = card ? ui::menuRect() : g.renderer.screenRect();
    ui::setFrame(r);
    S = r.height / 720.0f;
    OX = r.x; OY = r.y; WW = r.width; WH = r.height;
}
float X(float x) { return ui::X(x); }
float Y(float y) { return ui::Y(y); }

float widthOf(const std::string& s, float size, int font = ui::BODY)   // in page units
{
    return ui::textW(s, size, font, font == ui::PIXEL ? 0.06f : 0);
}

// align: 0 left, 1 centre, 2 right. font: ui::BODY (Inter) or ui::PIXEL (Pixelify Sans).
void text(const std::string& s, float x, float y, float size, Color c, int align = 0, int font = ui::BODY, bool bold = false)
{
    ui::TextStyle st;
    st.size = size;
    st.color = c;
    st.align = align;
    st.font = font;
    st.bold = bold;
    st.track = font == ui::PIXEL ? 0.06f : 0;
    ui::text(s, x, y, st);
}
void ptext(const std::string& s, float x, float y, float size, Color c, int align = 0) { text(s, x, y, size, c, align, (int)ui::PIXEL); }

// "Press any button", as a key / button glyph and a word, bottom right
void continuePrompt(float rightX, float y, float a)
{
    float k = 0.6f + 0.4f * (0.5f + 0.5f * std::sin((float)GetTime() * 3.5f));
    ui::Prompt p{{"Enter"}, {ui::PAD_A}, T("story_continue")};
    float w = ui::promptW(p, 24);
    ui::prompt(p, rightX - w, y, 24, fade(NARR, k), a);
}

std::vector<std::string> wrap(const std::string& s, float size, float maxW)
{
    std::vector<std::string> out;
    std::string line, word;
    auto flush = [&]() {
        if (word.empty()) return;
        std::string tryLine = line.empty() ? word : line + " " + word;
        if (!line.empty() && widthOf(tryLine, size) > maxW) { out.push_back(line); line = word; }
        else line = tryLine;
        word.clear();
    };
    for (char ch : s) {
        if (ch == ' ') flush();
        else if (ch == '\n') { flush(); out.push_back(line); line.clear(); }
        else word += ch;
    }
    flush();
    if (!line.empty()) out.push_back(line);
    return out;
}

bool has(const std::string& key) { return strings().get(key) != key; }

std::string memLabel(int id)
{
    char buf[64];
    std::snprintf(buf, sizeof buf, T("story_memory_of"), id + 1, MEMORIES);
    return buf;
}

}  // namespace

namespace { void unloadSceneFonts(); }

// ------------------------------------------------------------------ cards
void Story::unload()
{
    scene::unload();
    unloadSceneFonts();
    for (auto& p : photo_) if (p.id) { UnloadTexture(p); p = Texture2D{}; }
}

void Story::open(const std::vector<int>& cards)
{
    queue_ = cards;
    cur_ = 0;
    if (active()) load(queue_[0]);
}

void Story::load(int id)
{
    card_ = Card();
    voicedLine_ = -1;   // akshat
    card_.id = id;
    std::string pre = id == PROLOGUE ? "story_prologue" : "story_" + std::to_string(id);
    if (id == PROLOGUE) {
        card_.label = T("story_prologue_label");
        card_.title = T("story_prologue_title");
        card_.when = T("story_prologue_when");
    } else {
        card_.label = memLabel(id);
        card_.title = T("memory_title_" + std::to_string(id));
        card_.when = T("memory_when_" + std::to_string(id));
    }
    loadLines(pre);
}

namespace {
// the speaker and words a line's voiceover clip is keyed on (speech is shown in quotes)
const char* speakerOf(int who) { return who == 0 ? "gran" : (who == 1 ? "sam" : "narrator"); }
std::string voiceText(int who, const std::string& shown)
{
    std::string t = shown;
    if (who >= 0 && t.size() >= 2 && t.front() == '"' && t.back() == '"') t = t.substr(1, t.size() - 2);
    return t;
}
}  // namespace

// akshat: the lines of a card, each given the time it needs: long enough to read, and never
// shorter than its voiceover, so the voice is not cut off and the scene acts in step with it.
void Story::loadLines(const std::string& pre)
{
    for (int i = 1; has(pre + "_" + std::to_string(i)); ++i) {
        std::string s = T(pre + "_" + std::to_string(i));
        Line l{-1, s};
        if (s.rfind("GRAN:", 0) == 0) l = {0, s.substr(5)};
        else if (s.rfind("SAM:", 0) == 0) l = {1, s.substr(4)};
        if (l.who >= 0) {
            size_t a = l.text.find_first_not_of(' ');
            l.text = "\"" + (a == std::string::npos ? std::string() : l.text.substr(a)) + "\"";
        }
        card_.lines.push_back(l);
    }
    float t = FIRST_LINE;
    for (auto& l : card_.lines) {
        float n = (float)l.text.size();
        float voice = vo::length(speakerOf(l.who), voiceText(l.who, l.text));
        float read = 1.4f + n / 20.0f;
        l.start = t;
        l.dur = std::max(voice + 0.7f, std::max(n / CPS + 1.0f, read));
        t += l.dur;
        card_.starts.push_back(l.start);
    }
    card_.end = t;
}

int Story::lineAt() const
{
    int at = -1;
    for (int i = 0; i < (int)card_.lines.size(); ++i)
        if (card_.t >= card_.lines[i].start) at = i;
    return at;
}

// akshat: voiceover. The line on screen starts its clip once; a new card starts over.
void Story::voiceTick()
{
    int i = lineAt();
    if (i < 0 || i == voicedLine_) return;
    const Line& l = card_.lines[i];
    if (card_.t > l.start + l.dur) return;
    voicedLine_ = i;
    vo::play(speakerOf(l.who), voiceText(l.who, l.text));
}

// A button: the first press finishes typing the line, the next moves to the next line
// (the scene jumps along with it); after the last line it ends the card.
bool Story::press()
{
    if (card_.t < 0.35f) return false;
    if (typed()) return true;
    int i = lineAt();
    if (i < 0) { card_.t = card_.lines.empty() ? card_.end : card_.lines[0].start; return false; }
    const Line& l = card_.lines[i];
    float typedAt = l.start + l.text.size() / CPS;
    if (card_.t < typedAt) { card_.t = typedAt + 0.01f; return false; }
    vo::stop();
    card_.t = i + 1 < (int)card_.lines.size() ? card_.lines[i + 1].start : card_.end;
    return false;
}

void Story::update(float dt, bool pressed, bool autoplay)
{
    if (!active()) return;
    card_.t += dt;
    bool next = false;
    if (pressed && press()) next = true;
    if (autoplay && card_.t > card_.end + 0.8f) next = true;
    if (next) {
        ++cur_;
        vo::stop();          // akshat
        voicedLine_ = -1;
        if (active()) load(queue_[cur_]);
    }
    if (active()) voiceTick();   // akshat
}

void Story::setPhoto(int era, Image img)
{
    if (era < 0 || era >= MEMORIES) { UnloadImage(img); return; }
    // the pixel stages stay crisp; the smooth ones are smoothed down
    if (art::lowRes(era)) ImageResizeNN(&img, 640, 360);
    else ImageResize(&img, 640, 360);
    if (photo_[era].id) UnloadTexture(photo_[era]);
    photo_[era] = LoadTextureFromImage(img);
    SetTextureFilter(photo_[era], TEXTURE_FILTER_BILINEAR);
    UnloadImage(img);
}

void Story::drawPolaroid(Game& g, int era, float cx, float cy, float w, float angle, float alpha, const std::string& caption)
{
    const float b = w * 0.045f, pw = w - 2 * b, ph = pw * 9.0f / 16.0f, bottom = w * 0.15f;
    const float h = b + ph + bottom;
    const float Wp = w * S, Hp = h * S, Bp = b * S, PWp = pw * S, PHp = ph * S;
    rlPushMatrix();
    rlTranslatef(X(cx), Y(cy), 0);
    rlRotatef(angle, 0, 0, 1);
    float x0 = -Wp / 2, y0 = -Hp / 2;
    DrawRectangleRec({x0 + 6 * S, y0 + 9 * S, Wp, Hp}, fade(BLACK, 0.45f * alpha));
    DrawRectangleRec({x0, y0, Wp, Hp}, fade(PAPER, alpha));
    Rectangle dst{x0 + Bp, y0 + Bp, PWp, PHp};
    if (photo_[era].id) {
        DrawTexturePro(photo_[era], {0, 0, (float)photo_[era].width, (float)photo_[era].height}, dst, {0, 0}, 0, fade(WHITE, alpha));
    } else {
        // no photograph taken yet (a run started late): the stage's own backdrop instead
        DrawRectangleRec(dst, fade(BLACK, alpha));
        for (int l = 0; l < art::BG_LAYERS; ++l) {
            const Texture2D& t = g.art.era[era].bg[l];
            if (!t.id) continue;
            float sw = std::min((float)t.width, t.height * 16.0f / 9.0f);
            DrawTexturePro(t, {0, 0, sw, (float)t.height}, dst, {0, 0}, 0, fade(WHITE, alpha));
        }
    }
    // a strip of tape across the top edge
    DrawRectanglePro({0, y0 + 2 * S, Wp * 0.26f, 18 * S}, {Wp * 0.13f, 9 * S}, 3.5f, fade(Color{232, 220, 190, 255}, 0.75f * alpha));
    if (!caption.empty()) {
        float fsz = 0;
        const Font& f = ui::fontAt(w * 0.07f, ui::HAND, &fsz);   // written on the photo by hand
        float tw = MeasureTextEx(f, caption.c_str(), fsz, 0).x;
        DrawTextEx(f, caption.c_str(), {std::round(-tw / 2), std::round(y0 + Bp + PHp + (bottom * S - fsz) / 2)}, fsz, 0,
                   fade(PHOTO_INK, alpha));
    }
    rlPopMatrix();
}

// ---- the scenes' type: Lora (a book serif) for what is said, Lora Italic for the narrator,
// Caveat (handwriting, like a note on the back of a photograph) for years and labels.
// akshat: loaded here at the exact pixel size they are drawn at; switch to the Theme's font
// handles (ui::SERIF, ui::SERIF_ITALIC, ui::HAND) once it has them.
namespace {
enum SceneFont { F_SERIF, F_ITALIC, F_HAND };
std::map<int, Font> g_fonts;
const Font& sceneFont(int f, float sizeUI)
{
    int px = std::max(8, (int)std::lround(sizeUI * S * ui::dpi()));
    int key = f * 100000 + px;
    auto it = g_fonts.find(key);
    if (it != g_fonts.end()) return it->second;
    static const char* files[3] = {"fonts/Lora.ttf", "fonts/Lora-Italic.ttf", "fonts/Caveat.ttf"};
    std::vector<int> cps;
    for (int c = 32; c < 127; ++c) cps.push_back(c);
    for (int c : {0xB7, 0xE9, 0x2014, 0x2013, 0x2018, 0x2019, 0x201C, 0x201D, 0x2026}) cps.push_back(c);
    Font font = LoadFontEx(assetPath(files[f]).c_str(), px, cps.data(), (int)cps.size());
    if (font.texture.id) SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);
    else font = GetFontDefault();
    return g_fonts.emplace(key, font).first->second;
}
void unloadSceneFonts()
{
    for (auto& kv : g_fonts) if (kv.second.texture.id && kv.second.texture.id != GetFontDefault().texture.id) UnloadFont(kv.second);
    g_fonts.clear();
}
float fW(const std::string& s, float size, int f)   // in page units
{
    return MeasureTextEx(sceneFont(f, size), s.c_str(), size * S, 0).x / S;
}
// align 0 left, 1 centre, 2 right; a soft shadow under every line
void fText(const std::string& s, float x, float y, float size, Color c, int f, int align = 0, float shadow = 0.85f)
{
    const Font& font = sceneFont(f, size);
    float w = fW(s, size, f);
    if (align == 1) x -= w / 2;
    else if (align == 2) x -= w;
    Vector2 p{std::round(X(x)), std::round(Y(y))};
    if (shadow > 0) {
        Color sh = fade(BLACK, shadow * c.a / 255.0f);
        DrawTextEx(font, s.c_str(), {p.x + std::max(1.0f, 2 * S), p.y + std::max(1.0f, 2 * S)}, size * S, 0, sh);
    }
    DrawTextEx(font, s.c_str(), p, size * S, 0, c);
}
std::vector<std::string> fWrap(const std::string& s, float size, float maxW, int f)
{
    std::vector<std::string> out;
    std::string line, word;
    auto flush = [&]() {
        if (word.empty()) return;
        std::string t = line.empty() ? word : line + " " + word;
        if (!line.empty() && fW(t, size, f) > maxW) { out.push_back(line); line = word; }
        else line = t;
        word.clear();
    };
    for (char ch : s) { if (ch == ' ') flush(); else word += ch; }
    flush();
    if (!line.empty()) out.push_back(line);
    return out;
}
const Color CREAM = {246, 238, 222, 255};
const Color NARR_W = {232, 216, 188, 255};
const Color NAME_COL[2] = {{255, 168, 72, 255}, {104, 178, 255, 255}};   // Gran amber, Sam blue
}  // namespace

// The timeline, in the top letterbox bar: NOW on the right, her earliest memory on the left.
// restoredUpTo: memories 0..restoredUpTo-1 are back. current: the memory being shown (-1: none).
void Story::drawTimelineStrip(int current, int restoredUpTo, float alpha)
{
    const float y = 22, x0 = 360, x1 = 1020;
    auto xOf = [&](int era) { return x0 + (x1 - x0) * (MEMORIES - 1 - era) / (float)MEMORIES; };
    ui::rect({x0, y - 0.5f, x1 - x0, 1.5f}, fade(DIM, alpha));
    float reach = current >= 0 ? xOf(current) : (restoredUpTo > 0 ? xOf(restoredUpTo - 1) : x1);
    ui::rect({reach, y - 1, x1 - reach, 2.5f}, fade(CREAM, alpha * 0.8f));
    fText("further back", x0 - 22, y - 16, 26, fade(NARR_W, alpha * 0.8f), F_HAND, 2, 0);
    for (int e = 0; e < MEMORIES; ++e) {
        float x = xOf(e);
        Color c = fade(NARR_W, 0.55f);
        if (e == current) {
            float pulse = 0.5f + 0.5f * std::sin((float)GetTime() * 4);
            DrawCircleV({X(x), Y(y)}, (9 + 2 * pulse) * S, fade(AMBER, alpha * 0.3f));
            DrawCircleV({X(x), Y(y)}, 6 * S, fade(AMBER, alpha));
            c = AMBER;
        } else if (e < restoredUpTo) {
            DrawCircleV({X(x), Y(y)}, 4.5f * S, fade(CREAM, alpha));
            c = CREAM;
        } else {
            DrawCircleV({X(x), Y(y)}, 3.5f * S, fade(DIM, alpha));
        }
        fText(T("memory_year_" + std::to_string(e)), x, y + 6, 26, fade(c, alpha), F_HAND, 1, 0);
    }
    Color nc = current < 0 ? AMBER : CREAM;
    ui::rect({x1 - 5, y - 5, 10, 10}, fade(nc, alpha));
    fText("now", x1, y + 6, 26, fade(nc, alpha), F_HAND, 1, 0);
}

// The opening caption, like the note on the back of a photograph: a handwritten label,
// the title in Lora, the year in handwriting. Lower left, fading away.
void Story::drawCaption(float a)
{
    if (a <= 0) return;
    const float x = 92, w = 820;
    auto rows = fWrap(card_.title, 68, w, F_SERIF);
    float y = 720 - BAR_BOT - 44 - rows.size() * 78 - 100;
    // a soft shade behind the words so they read on any scene
    float top = y - 60, h = 720 - BAR_BOT - top;
    ui::gradV({0, top, 1280, h * 0.45f}, fade(BLACK, 0), fade(BLACK, 0.45f * a));
    ui::gradV({0, top + h * 0.45f, 1280, h * 0.55f + 1}, fade(BLACK, 0.45f * a), fade(BLACK, 0.75f * a));
    std::string label = card_.label;
    for (auto& ch : label) ch = (char)std::tolower((unsigned char)ch);
    if (!label.empty()) label[0] = (char)std::toupper((unsigned char)label[0]);
    fText(label, x, y, 36, fade(AMBER, a), F_HAND);
    y += 46;
    for (auto& r : rows) { fText(r, x, y, 68, fade(CREAM, a), F_SERIF); y += 78; }
    std::string when = card_.when;
    for (size_t i = 0; (i = when.find(" . ", i)) != std::string::npos;) when.replace(i, 3, "  \xC2\xB7  ");
    // "1979 . EDIE IS 33" -> "1979 · Edie is 33": sentence case, as if written by hand
    std::string w2;
    bool cap = true;
    for (char ch : when) {
        unsigned char u = (unsigned char)ch;
        if (std::isalpha(u)) { w2 += cap ? (char)std::toupper(u) : (char)std::tolower(u); cap = false; }
        else { w2 += ch; if (u == 0xB7) cap = true; }
    }
    for (size_t i = 0; (i = w2.find("edie", i)) != std::string::npos; ++i) w2[i] = 'E';
    fText(w2, x + 2, y + 4, 42, fade(AMBER, a), F_HAND);
}

// The line being spoken, typed out in the bottom letterbox bar: the speaker's name in their
// colour, then the words in Lora. Narration is in Lora Italic, with no name.
void Story::drawSubtitle(float a)
{
    int i = lineAt();
    if (i < 0) return;
    const Line& l = card_.lines[i];
    float since = card_.t - l.start;
    float out = i + 1 == (int)card_.lines.size() ? 1.0f : (l.start + l.dur - card_.t) / 0.3f;
    float k = std::max(0.0f, std::min(1.0f, std::min(since / 0.25f, out))) * a;
    if (k <= 0) return;
    size_t shown = (size_t)std::max(0.0f, since * CPS);
    const int f = l.who >= 0 ? F_SERIF : F_ITALIC;
    std::string name = l.who >= 0 ? T(l.who == 0 ? "p1_name" : "p2_name") : "";
    if (!name.empty()) { for (auto& ch : name) ch = (char)std::tolower((unsigned char)ch); name[0] = (char)std::toupper((unsigned char)name[0]); }
    float size = 32;
    const float nameSize = 36;
    float nameW = name.empty() ? 0 : fW(name, nameSize, F_HAND) + 18;
    auto rows = fWrap(l.text, size, 1120 - nameW, f);
    if (rows.size() > 2) { size = 29; rows = fWrap(l.text, size, 1180 - nameW, f); }
    float lh = size * 1.22f;
    float blockW = 0;
    for (auto& r : rows) blockW = std::max(blockW, fW(r, size, f));
    float x = 640 - (blockW + nameW) / 2;
    float y = 720 - BAR_BOT / 2 - rows.size() * lh / 2 + 2;
    if (!name.empty()) fText(name, x, y - (nameSize - size) * 0.45f - 2, nameSize, fade(NAME_COL[l.who], k), F_HAND);
    size_t budget = shown;
    for (auto& r : rows) {
        size_t n = std::min(r.size(), budget);
        if (n > 0) fText(r.substr(0, n), x + nameW, y, size, fade(l.who >= 0 ? CREAM : NARR_W, k), f);
        budget = budget > r.size() + 1 ? budget - r.size() - 1 : 0;
        y += lh;
    }
}

void Story::drawScene(Game& g, int restoredUpTo)
{
    page(g);
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), BLACK);   // the room is dark
    Rectangle r = ui::menuRect();
    scene::draw(g, card_.id, card_.t, card_.starts, card_.end, r);
    // letterbox bars, sliding in as the scene opens
    float k = ui::easeOutCubic(std::min(1.0f, card_.t / 0.8f));
    const Color bar = {10, 8, 7, 255};
    ui::rect({0, -BAR_TOP * (1 - k), 1280, BAR_TOP}, bar);
    ui::rect({0, 720 - BAR_BOT * k, 1280, BAR_BOT}, bar);
    const float t = card_.t;
    float cap = std::min(1.0f, std::max(0.0f, (t - 0.7f) / 0.8f)) * std::min(1.0f, std::max(0.0f, (FIRST_LINE + 2.6f - t) / 0.9f));
    drawCaption(cap);
    int cur = card_.id >= 0 && card_.id < MEMORIES ? card_.id : -1;
    float ta = std::min(1.0f, std::max(0.0f, (t - 0.5f) / 0.8f));
    drawTimelineStrip(cur, restoredUpTo, ta * 0.9f);
    drawSubtitle(1);
    if (typed()) continuePrompt(1250, 720 - 30, std::min(1.0f, (t - card_.end) / 0.4f));
}

void Story::drawCard(Game& g)
{
    if (!active()) return;
    int cur = card_.id;
    drawScene(g, cur < 0 ? 0 : cur);
}

// ------------------------------------------------------------------ cinematic bars
void Story::showBars(const std::string& label, const std::string& title, const std::string& line, float seconds)
{
    barsLabel_ = label;
    barsTitle_ = title;
    barsLine_ = line;
    barsT_ = barsLen_ = seconds;
}

void Story::drawBars(Game& g)
{
    if (barsT_ <= 0) return;
    page(g, false);
    float into = barsLen_ - barsT_;
    float k = std::min(1.0f, std::min(into / 0.35f, barsT_ / 0.35f));
    k = 1 - (1 - k) * (1 - k);
    const float bh = 84 * S;
    DrawRectangleRec({OX, OY - bh * (1 - k), WW, bh}, fade(BLACK, 0.92f));
    DrawRectangleRec({OX, OY + WH - bh * k, WW, bh}, fade(BLACK, 0.92f));
    float ta = std::max(0.0f, std::min(1.0f, (into - 0.25f) / 0.3f)) * std::min(1.0f, barsT_ / 0.3f);
    // akshat (UI pass): a handwritten label above, the title in Lora, the line in Lora Italic
    text(barsLabel_, 640, -84 * (1 - k) + 22, 34, fade(AMBER, ta), 1, (int)ui::HAND);
    float by = 720 - 84 * k;
    text(barsTitle_, 640, by + 6, 34, fade(PAPER, ta), 1, (int)ui::SERIF, true);
    if (!barsLine_.empty()) text(barsLine_, 640, by + 46, 25, fade(NARR, ta), 1, (int)ui::SERIF_ITALIC);
}

// ------------------------------------------------------------------ the ending
void Story::startEnding()
{
    endPhase_ = 0;
    endT_ = 0;
    queue_.clear();
    cur_ = -1;
}

void Story::loadEnding()
{
    card_ = Card();
    voicedLine_ = -1;   // akshat
    card_.id = scene::ENDING;
    card_.label = T("end_label");
    card_.title = T("end_title");
    card_.when = T("end_when");
    loadLines("end");
}

namespace {
constexpr float ALBUM_STEP = 0.7f;
float albumDone() { return 1.0f + MEMORIES * ALBUM_STEP + 1.0f; }
}

bool Story::updateEnding(float dt, bool pressed, bool autoplay)
{
    endT_ += dt;
    if (endPhase_ == 0) {
        if ((pressed && endT_ > albumDone()) || (autoplay && endT_ > albumDone() + 2.0f)) {
            endPhase_ = 1; endT_ = 0; loadEnding();
        }
    } else if (endPhase_ == 1) {
        card_.t += dt;
        bool done = pressed && press();
        if (done || (autoplay && card_.t > card_.end + 1.5f)) { endPhase_ = 2; endT_ = 0; vo::stop(); voicedLine_ = -1; }
        else voiceTick();   // akshat
    } else {
        if (pressed && endT_ > 2.5f && !autoplay) return true;
    }
    return false;
}

void Story::drawEnding(Game& g)
{
    page(g);
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), INK);
    ui::vignette({0, 0, 1280, 720}, 0.35f);
    if (endPhase_ == 0) {
        // the album: oldest memory first, in the order her life went
        float a = std::min(1.0f, endT_ / 0.8f);
        ptext(T("end_album_title"), 640, 40, 40, fade(PAPER, a), 1);
        text(T("end_album_sub"), 640, 96, 19, fade(NARR, a), 1);
        const float xs[MEMORIES] = {200, 493, 787, 1080, 347, 640, 933};
        const float ys[MEMORIES] = {262, 262, 262, 262, 500, 500, 500};
        const float ang[MEMORIES] = {-4, 3, -2, 4.5f, -3, 2, -1.5f};
        for (int k = 0; k < MEMORIES; ++k) {
            int era = MEMORIES - 1 - k;
            float at = 1.0f + k * ALBUM_STEP;
            float pa = std::min(1.0f, std::max(0.0f, (endT_ - at) / 0.4f));
            if (pa <= 0) continue;
            drawPolaroid(g, era, xs[k], ys[k] - (1 - pa) * 20, 250, ang[k], pa, T("memory_year_" + std::to_string(era)));
        }
        if (endT_ > albumDone()) continuePrompt(1184, 668, 1);
        return;
    }
    if (endPhase_ == 1) {
        drawScene(g, MEMORIES);
        return;
    }
    // closing words, then the credits
    float a = std::min(1.0f, endT_ / 1.0f);
    float y = 110;
    for (int i = 1; has("end_close_" + std::to_string(i)); ++i) {
        for (auto& r : wrap(T("end_close_" + std::to_string(i)), 20, 900)) { text(r, 640, y, 21, fade(PAPER, a), 1); y += 31; }
        y += 8;
    }
    float b = std::min(1.0f, std::max(0.0f, (endT_ - 1.2f) / 1.0f));
    y += 34;
    ptext(T("credits_title"), 640, y, 40, fade(AMBER, b), 1);
    y += 62;
    std::string credits = T("credits");
    size_t start = 0;
    while (start <= credits.size()) {
        size_t nl = credits.find('\n', start);
        std::string line = credits.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        if (!line.empty()) text(line, 640, y, 18, fade(NARR, b), 1);
        y += line.empty() ? 12 : 28;
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    text(T("thanks"), 640, y + 34, 20, fade(PAPER, b), 1, (int)ui::BODY, true);
    if (endT_ > 2.5f) {
        float k = 0.6f + 0.4f * (0.5f + 0.5f * std::sin((float)GetTime() * 3.5f));
        ui::Prompt p{{"Enter"}, {ui::PAD_A}, T("ending_continue")};
        ui::prompt(p, 1184 - ui::promptW(p, 24), 668, 24, fade(NARR, k), 1);
    }
}
