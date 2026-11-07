#include "audio/Voice.h"
#include "ui/Story.h"
#include "core/Game.h"
#include "core/Strings.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

// The story UI has one look on every stage: a dark, warm room with a photograph in it.
// The photograph is the only thing that changes, because it is taken from the game itself:
// the memories get clearer, the photographs get clearer. Everything is laid out on a
// 1280 x 720 page and scaled to the game image; text uses the default font at whole scales.

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

constexpr float CPS = 50.0f;        // characters typed per second
constexpr float TYPE_DELAY = 0.6f;  // the card settles before the words start
constexpr int LINE_PAUSE = 14;      // a pause between lines, counted in characters
constexpr int MEMORIES = art::ERA_COUNT;

Color fade(Color c, float a) { c.a = (unsigned char)(c.a * std::max(0.0f, std::min(1.0f, a))); return c; }

void page(Game& g)
{
    Rectangle r = g.renderer.screenRect();
    S = r.height / 720.0f;
    OX = r.x; OY = r.y; WW = r.width; WH = r.height;
}
float X(float x) { return std::round(OX + x * S); }
float Y(float y) { return std::round(OY + y * S); }
int scaleFor(float size) { return std::max(1, (int)std::lround(size * S / 10.0f)); }

float widthOf(const std::string& s, float size)   // in page units
{
    int t = scaleFor(size);
    return MeasureTextEx(GetFontDefault(), s.c_str(), 10.0f * t, (float)t).x / S;
}

// align: 0 left, 1 centre, 2 right
void text(const std::string& s, float x, float y, float size, Color c, int align = 0)
{
    int t = scaleFor(size);
    float w = MeasureTextEx(GetFontDefault(), s.c_str(), 10.0f * t, (float)t).x;
    float px = X(x) - (align == 1 ? std::round(w / 2) : align == 2 ? w : 0);
    DrawTextEx(GetFontDefault(), s.c_str(), {px, Y(y)}, 10.0f * t, (float)t, c);
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

// ------------------------------------------------------------------ cards
void Story::unload()
{
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
    for (auto& l : card_.lines) card_.totalChars += (int)l.text.size() + LINE_PAUSE;
}

// akshat: voiceover. The line being typed right now starts its clip once; a new card starts over.
void Story::voiceTick()
{
    float budget = shownChars();
    for (int i = 0; i < (int)card_.lines.size(); ++i) {
        const Line& l = card_.lines[i];
        if (budget < (float)l.text.size() + LINE_PAUSE) {
            if (i != voicedLine_) {
                voicedLine_ = i;
                std::string t = l.text;   // speech is shown in one pair of quotes: voice the words inside
                if (l.who >= 0 && t.size() >= 2 && t.front() == '"' && t.back() == '"') t = t.substr(1, t.size() - 2);
                vo::play(l.who == 0 ? "gran" : (l.who == 1 ? "sam" : "narrator"), t);
            }
            return;
        }
        budget -= l.text.size() + LINE_PAUSE;
    }
}

float Story::shownChars() const { return std::max(0.0f, (card_.t - TYPE_DELAY) * CPS); }

void Story::update(float dt, bool pressed, bool autoplay)
{
    if (!active()) return;
    card_.t += dt;
    float typedAt = TYPE_DELAY + card_.totalChars / CPS;
    bool next = false;
    if (pressed && card_.t > 0.35f) {
        if (!typed()) card_.t = typedAt + 0.01f;   // first press shows all the words
        else next = true;
    }
    if (autoplay && card_.t > typedAt + 1.5f) next = true;
    if (next) {
        ++cur_;
        vo::stop();   // akshat
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
        int t = scaleFor(20);
        float tw = MeasureTextEx(GetFontDefault(), caption.c_str(), 10.0f * t, (float)t).x;
        DrawTextEx(GetFontDefault(), caption.c_str(), {std::round(-tw / 2), std::round(y0 + Bp + PHp + (bottom * S - 10 * t) / 2)},
                   10.0f * t, (float)t, fade(PHOTO_INK, alpha));
    }
    rlPopMatrix();
}

// The timeline along the bottom: NOW on the right, her earliest memory on the left.
// restoredUpTo: memories 0..restoredUpTo-1 are back. current: the memory this card is about (-1: none).
void Story::drawTimeline(Game& g, int current, int restoredUpTo, float alpha)
{
    (void)g;
    const float y = 640, x0 = 150, x1 = 1130;
    auto xAt = [&](int i) { return x0 + (x1 - x0) * i / (float)MEMORIES; };   // i 0..6 = memory 6..0, i 7 = now
    auto xOf = [&](int era) { return xAt(MEMORIES - 1 - era); };
    DrawRectangleRec({X(x0), Y(y) - std::max(1.0f, S), (x1 - x0) * S, std::max(1.0f, 2 * S)}, fade(DIM, alpha));
    float reach = current >= 0 ? xOf(current) : (restoredUpTo > 0 ? xOf(restoredUpTo - 1) : x1);
    DrawRectangleRec({X(reach), Y(y) - 2 * S, (x1 - reach) * S, 4 * S}, fade(PAPER, alpha));
    text(T("story_further_back"), x0 - 6, y - 40, 20, fade(DIM, alpha));
    for (int e = 0; e < MEMORIES; ++e) {
        float x = xOf(e);
        Color c = DIM;
        if (e == current) {
            float pulse = 0.5f + 0.5f * std::sin((float)GetTime() * 4);
            DrawCircleV({X(x), Y(y)}, (11 + 2 * pulse) * S, fade(AMBER, alpha * 0.35f));
            DrawCircleV({X(x), Y(y)}, 7 * S, fade(AMBER, alpha));
            c = AMBER;
        } else if (e < restoredUpTo) {
            DrawCircleV({X(x), Y(y)}, 6 * S, fade(PAPER, alpha));
            c = PAPER;
        } else {
            DrawCircleV({X(x), Y(y)}, 4 * S, fade(DIM, alpha));
        }
        text(T("memory_year_" + std::to_string(e)), x, y + 18, 20, fade(c, alpha), 1);
    }
    DrawRectangleRec({X(x1) - 6 * S, Y(y) - 6 * S, 12 * S, 12 * S}, fade(current < 0 ? AMBER : PAPER, alpha));
    text(T("story_now"), x1, y + 18, 20, fade(current < 0 ? AMBER : PAPER, alpha), 1);
}

void Story::drawLines(Game& g, float x, float y, float w, float alpha)
{
    (void)g;
    float budget = shownChars();
    const float lh = 30, tagW = 92;
    for (auto& l : card_.lines) {
        if (budget <= 0) break;
        float avail = l.who >= 0 ? w - tagW : w;
        auto rows = wrap(l.text, 20, avail);
        if (l.who >= 0) text(T(l.who == 0 ? "p1_name" : "p2_name"), x, y, 20, fade(WHO_COL[l.who], alpha));
        float remain = budget;
        for (auto& r : rows) {
            if (remain <= 0) break;
            size_t n = (size_t)std::min((float)r.size(), remain);
            text(r.substr(0, n), x + (l.who >= 0 ? tagW : 0), y, 20, fade(l.who >= 0 ? PAPER : NARR, alpha));
            remain -= r.size() + 1;
            y += lh;
        }
        y += 14;
        budget -= l.text.size() + LINE_PAUSE;
    }
}

void Story::drawCard(Game& g)
{
    if (!active()) return;
    page(g);
    DrawRectangleRec({OX, OY, WW, WH}, INK);
    const float t = card_.t;
    const float a = std::min(1.0f, t / 0.4f);
    const bool photo = card_.id >= 0;

    float x = photo ? 96 : 300, w = photo ? 560 : 680;
    if (photo) {
        float k = std::min(1.0f, t / 0.7f);
        k = 1 - (1 - k) * (1 - k);
        drawPolaroid(g, card_.id, 935, 330 + (1 - k) * 40, 520, -2.5f + (1 - k) * 4, a, card_.title);
    }
    float y = photo ? 92 : 96;
    text(card_.label, x, y, 20, fade(DIM, a));
    y += 34;
    for (auto& r : wrap(card_.title, 40, w)) { text(r, x, y, 40, fade(PAPER, a)); y += 48; }
    y += 6;
    text(card_.when, x, y, 20, fade(AMBER, a));
    y += 56;
    drawLines(g, x, y, w, a);

    if (typed() && std::fmod((float)GetTime(), 1.2f) < 0.8f) text(T("story_continue"), 1184, 584, 20, fade(NARR, a), 2);
    int cur = card_.id;
    drawTimeline(g, cur, cur < 0 ? 0 : cur, a);
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
    page(g);
    float into = barsLen_ - barsT_;
    float k = std::min(1.0f, std::min(into / 0.35f, barsT_ / 0.35f));
    k = 1 - (1 - k) * (1 - k);
    const float bh = 84 * S;
    DrawRectangleRec({OX, OY - bh * (1 - k), WW, bh}, fade(BLACK, 0.92f));
    DrawRectangleRec({OX, OY + WH - bh * k, WW, bh}, fade(BLACK, 0.92f));
    float ta = std::max(0.0f, std::min(1.0f, (into - 0.25f) / 0.3f)) * std::min(1.0f, barsT_ / 0.3f);
    text(barsLabel_, 640, -84 * (1 - k) + 34, 20, fade(AMBER, ta), 1);
    float by = 720 - 84 * k;
    text(barsTitle_, 640, by + 14, 30, fade(LILAC, ta), 1);
    if (!barsLine_.empty()) text(barsLine_, 640, by + 52, 20, fade(PAPER, ta), 1);
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
    card_.id = PROLOGUE;
    card_.label = T("end_label");
    card_.title = T("end_title");
    card_.when = T("end_when");
    for (int i = 1; has("end_" + std::to_string(i)); ++i) {
        std::string s = T("end_" + std::to_string(i));
        Line l{-1, s};
        if (s.rfind("GRAN:", 0) == 0) l = {0, s.substr(5)};
        else if (s.rfind("SAM:", 0) == 0) l = {1, s.substr(4)};
        if (l.who >= 0) {
            size_t a = l.text.find_first_not_of(' ');
            l.text = "\"" + (a == std::string::npos ? std::string() : l.text.substr(a)) + "\"";
        }
        card_.lines.push_back(l);
    }
    for (auto& l : card_.lines) card_.totalChars += (int)l.text.size() + LINE_PAUSE;
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
        voiceTick();   // akshat
        float typedAt = TYPE_DELAY + card_.totalChars / CPS;
        if (pressed && card_.t > 0.35f) {
            if (!typed()) card_.t = typedAt + 0.01f;
            else { endPhase_ = 2; endT_ = 0; }
        }
        if (autoplay && card_.t > typedAt + 2.0f) { endPhase_ = 2; endT_ = 0; }
    } else {
        if (pressed && endT_ > 2.5f && !autoplay) return true;
    }
    return false;
}

void Story::drawEnding(Game& g)
{
    page(g);
    DrawRectangleRec({OX, OY, WW, WH}, INK);
    if (endPhase_ == 0) {
        // the album: oldest memory first, in the order her life went
        float a = std::min(1.0f, endT_ / 0.8f);
        text(T("end_album_title"), 640, 46, 40, fade(PAPER, a), 1);
        text(T("end_album_sub"), 640, 98, 20, fade(NARR, a), 1);
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
        if (endT_ > albumDone() && std::fmod((float)GetTime(), 1.2f) < 0.8f) text(T("story_continue"), 1184, 676, 20, NARR, 2);
        return;
    }
    if (endPhase_ == 1) {
        float a = std::min(1.0f, card_.t / 0.4f);
        float x = 300, w = 680, y = 96;
        text(card_.label, x, y, 20, fade(DIM, a));
        y += 34;
        for (auto& r : wrap(card_.title, 40, w)) { text(r, x, y, 40, fade(PAPER, a)); y += 48; }
        y += 6;
        text(card_.when, x, y, 20, fade(AMBER, a));
        y += 56;
        drawLines(g, x, y, w, a);
        if (typed() && std::fmod((float)GetTime(), 1.2f) < 0.8f) text(T("story_continue"), 1184, 584, 20, fade(NARR, a), 2);
        drawTimeline(g, -1, MEMORIES, a);
        return;
    }
    // closing words, then the credits
    float a = std::min(1.0f, endT_ / 1.0f);
    float y = 110;
    for (int i = 1; has("end_close_" + std::to_string(i)); ++i) {
        for (auto& r : wrap(T("end_close_" + std::to_string(i)), 20, 900)) { text(r, 640, y, 20, fade(PAPER, a), 1); y += 30; }
        y += 8;
    }
    float b = std::min(1.0f, std::max(0.0f, (endT_ - 1.2f) / 1.0f));
    y += 34;
    text(T("credits_title"), 640, y, 40, fade(AMBER, b), 1);
    y += 62;
    std::string credits = T("credits");
    size_t start = 0;
    while (start <= credits.size()) {
        size_t nl = credits.find('\n', start);
        std::string line = credits.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        if (!line.empty()) text(line, 640, y, 20, fade(NARR, b), 1);
        y += line.empty() ? 12 : 28;
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    text(T("thanks"), 640, y + 40, 20, fade(PAPER, b), 1);
    if (endT_ > 2.5f && std::fmod((float)GetTime(), 1.2f) < 0.8f) text(T("ending_continue"), 1184, 676, 20, NARR, 2);
}
