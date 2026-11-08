#include "audio/Voice.h"
#include "ui/Cutscene.h"
#include "core/Game.h"
#include "core/Strings.h"
#include "raylib.h"
#include "core/Paths.h"
#include "render/ArtSpec.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace {

// ---- layout (UI units, 1280 x 720)
constexpr float BOX_X = 60, BOX_Y = 516, BOX_W = 1160, BOX_H = 162;
constexpr float TEXT_SIZE = 28, LINE_H = 40, TEXT_PAD_X = 44, TEXT_PAD_Y = 26;
constexpr int LINES_PER_PAGE = 3;
constexpr float PORTRAIT_H = 300, PORTRAIT_BOTTOM = BOX_Y + 14;
constexpr float LEFT_X = 210, RIGHT_X = 1070;

// ---- encounter timeline (seconds)
constexpr float FLASH_END = 0.6f, WIPE_END = 1.15f, BANDS_END = 1.6f, SLAM_AT = 1.65f, TALK_AT = 2.9f;
constexpr float OUTRO_LEN = 0.6f, GRACE = 0.35f, HOLD_SKIP = 0.9f, AUTO_WAIT = 1.2f;

float S = 1, OX = 0, OY = 0;
void setup(Game& g)
{
    Rectangle r = g.renderer.screenRect();
    S = r.height / 720.0f;
    OX = r.x;
    OY = r.y;
    ui::setFrame(r);
}
int g_font = ui::PIXEL;   // akshat (UI pass): Pixelify Sans in the pixel stages, Inter in the smooth ones
int fontFor(int era) { return art::lowRes(era) ? ui::PIXEL : ui::BODY; }
Color hexc(unsigned h, unsigned char a = 255) { return Color{(unsigned char)(h >> 16), (unsigned char)(h >> 8), (unsigned char)h, a}; }
Color fade(Color c, float a) { c.a = (unsigned char)(c.a * std::max(0.0f, std::min(1.0f, a))); return c; }
Rectangle R(float x, float y, float w, float h) { return Rectangle{OX + x * S, OY + y * S, w * S, h * S}; }
float clamp01(float v) { return std::max(0.0f, std::min(1.0f, v)); }
float easeOut(float k) { k = clamp01(k); return 1 - (1 - k) * (1 - k) * (1 - k); }
int fs(float size) { return std::max(8, (int)std::lround(size * S)); }

// centre: x, y is the middle of the text. outline: big VS titles.
void text(const std::string& s, float x, float y, float size, Color c, bool shadow, bool centre = false, float scale = 1,
          Color outline = BLANK, int font = -1)
{
    ui::TextStyle st;
    st.size = size * scale;
    st.font = font < 0 ? g_font : font;
    st.track = st.font == ui::PIXEL ? 0.04f : 0;
    st.color = c;
    st.align = centre ? ui::CENTER : ui::LEFT;
    if (shadow) { st.shadow = fade(BLACK, c.a / 255.0f * 0.6f); st.shadowOff = std::max(2.0f, size * scale * 0.06f); }
    if (outline.a) { st.outline = outline; st.outlineW = std::max(2.0f, size * scale * 0.05f); }
    if (centre) ui::textMid(s, x, y, st);
    else ui::text(s, x, y, st);
}
float textW(const std::string& s, float size) { return ui::textW(s, size, g_font, g_font == ui::PIXEL ? 0.04f : 0); }

// A filled quad in UI units; drawn with both windings so the vertex order never matters.
void quad(Vector2 a, Vector2 b, Vector2 c, Vector2 d, Color col)
{
    auto P = [](Vector2 v) { return Vector2{OX + v.x * S, OY + v.y * S}; };
    Vector2 A = P(a), B = P(b), C = P(c), D = P(d);
    DrawTriangle(A, B, C, col); DrawTriangle(A, C, B, col);
    DrawTriangle(A, C, D, col); DrawTriangle(A, D, C, col);
}

struct Voice { const char* id; float pitch, cps; unsigned col; };
const Voice VOICES[] = {
    {"gran", 0.82f, 40, 0xf28a1e},
    {"sam", 1.32f, 44, 0x3c9cf0},
    {"school_bell", 1.18f, 34, 0xd8a030},
    {"last_train", 0.62f, 30, 0x3a9a5c},
    {"forgetting", 0.5f, 26, 0xa070e0},
    {"narrator", 1.0f, 40, 0xb8b0c8},
};
const Voice& voice(const std::string& id)
{
    for (const auto& v : VOICES) if (id == v.id) return v;
    return VOICES[5];
}
// The three bosses and the stage each belongs to (Boss::kind).
int bossKindOf(const std::string& id) { return id == "school_bell" ? 2 : id == "last_train" ? 4 : id == "forgetting" ? 6 : -1; }
bool isBoss(const std::string& id) { return bossKindOf(id) >= 0; }
std::string speakerName(const std::string& id)
{
    if (id == "gran") return T("p1_name");
    if (id == "sam") return T("p2_name");
    if (isBoss(id)) return T("boss_name_" + std::to_string(bossKindOf(id)));
    return "";
}
// The VS band colour behind each boss.
unsigned bandColour(const std::string& id)
{
    if (id == "school_bell") return 0xc8862a;
    if (id == "last_train") return 0x2a6e44;
    return 0x7a4ab8;
}
// Dialogue-box look for each stage: 0 1-bit / 4-shade, 1 8-bit, 2 16 / 32-bit, 3 2.5D / 3D.
int boxStyle(int era) { return era <= 1 ? 0 : era == 2 ? 1 : era <= 4 ? 2 : 3; }

std::string trim(std::string s)
{
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return s.substr(i);
}

// Draw a sprite frame scaled up crisply (whole-number scale for pixel art) standing at (cx, bottom).
void sprite(const Texture2D& tex, Rectangle src, int era, float cx, float bottom, float heightUI, bool flip, Color tint)
{
    float scale = heightUI * S / src.height;
    if (art::lowRes(era)) scale = std::max(1.0f, std::round(scale));
    float w = src.width * scale, h = src.height * scale;
    if (flip) src.width = -src.width;
    float x = std::round(OX + cx * S - w / 2), y = std::round(OY + bottom * S - h);
    DrawTexturePro(tex, src, Rectangle{x, y, w, h}, {0, 0}, 0, tint);
}

// A boss sheet is one row of square frames (float, float, hurt, slam).
Rectangle squareFrame(const Texture2D& t, int f) { float h = (float)t.height; return Rectangle{f * h, 0, h, h}; }

Texture2D* g_hourglass = nullptr;   // the Cutscene's own Hourglass portrait (stage 6 has no boss sheet)

void portrait(Game& g, const std::string& who, float cx, float bottom, float h, bool flip, Color tint, bool talking, float t,
              bool hurt)
{
    int e = g.era;
    const EraArt& A = g.art.era[e];
    if (who == "gran" || who == "sam") {
        const Texture2D& tex = A.player[who == "gran" ? 0 : 1];
        int fr = talking ? art::PF_IDLE0 + ((int)(t * 5) % 2) : art::PF_IDLE0;
        sprite(tex, frameRect(tex, fr, art::PLAYER_FW, art::PLAYER_FH, e), e, cx, bottom, h, flip, tint);
    } else if (isBoss(who)) {
        // this memory's own boss sheet; the full-3D stage uses its Forgetting sprite (enemy_boss.png)
        const bool own = A.storyBoss.id && bossKindOf(who) == e;
        const Texture2D* tex = own ? &A.storyBoss : nullptr;
        bool smooth = false;
        if (!tex) tex = A.storyBoss.id ? &A.storyBoss : &A.boss;
        // the hourglass sheet's fourth frame is its dizzy face; the others' is their grey hurt frame
        int hurtFr = smooth ? art::XF_SLAM : art::XF_HURT;
        int fr = hurt ? hurtFr : (talking && !smooth ? art::XF_FLOAT0 + ((int)(t * 4) % 2) : art::XF_FLOAT0);
        sprite(*tex, squareFrame(*tex, fr), smooth ? art::LAST_ERA : e, cx, bottom, h, flip, tint);
    }
}

// The dialogue box, restyled per era.
struct BoxStyle { Color text; bool shadow; };
BoxStyle drawBox(int stage, float x, float y, float w, float h)
{
    const int era = boxStyle(stage);
    if (era == 0) {   // 1-bit: cream and black, double pixel border
        DrawRectangleRec(R(x, y, w, h), hexc(0x101216));
        DrawRectangleRec(R(x + 6, y + 6, w - 12, h - 12), hexc(0xebebe1));
        DrawRectangleLinesEx(R(x + 11, y + 11, w - 22, h - 22), std::max(1.0f, 3 * S), hexc(0x101216));
        return {hexc(0x101216), false};
    }
    if (era == 1) {   // 8-bit: chunky, stepped corners, cream inside
        const float c = 8;
        DrawRectangleRec(R(x + c, y, w - 2 * c, h), hexc(0x0f0d1a));
        DrawRectangleRec(R(x, y + c, w, h - 2 * c), hexc(0x0f0d1a));
        DrawRectangleRec(R(x + c, y + 6, w - 2 * c, h - 12), hexc(0xf8f0d8));
        DrawRectangleRec(R(x + 6, y + c + 4, w - 12, h - 2 * c - 8), hexc(0xf8f0d8));
        DrawRectangleRec(R(x + 14, y + 14, w - 28, 4), hexc(0x26305e));
        DrawRectangleRec(R(x + 14, y + h - 18, w - 28, 4), hexc(0x26305e));
        DrawRectangleRec(R(x + 14, y + 14, 4, h - 28), hexc(0x26305e));
        DrawRectangleRec(R(x + w - 18, y + 14, 4, h - 28), hexc(0x26305e));
        return {hexc(0x0f0d1a), false};
    }
    if (era == 2) {   // 16-bit: SNES RPG blue gradient with a silver rounded frame
        DrawRectangleRounded(R(x, y, w, h), 0.14f, 8, hexc(0x0a0c20));
        DrawRectangleRounded(R(x + 3, y + 3, w - 6, h - 6), 0.13f, 8, hexc(0xd8e0f0));
        DrawRectangleRounded(R(x + 8, y + 8, w - 16, h - 16), 0.12f, 8, hexc(0x2a3c98));
        Rectangle in = R(x + 14, y + 12, w - 28, h - 24);
        DrawRectangleGradientV((int)in.x, (int)in.y, (int)in.width, (int)in.height, hexc(0x3a56c0), hexc(0x111a58));
        return {hexc(0xfdf8ff), true};
    }
    // 2.5D / 3D: the menus' dark glass, with a warm accent
    ui::panel({x, y, w, h}, ui::menuSkin());
    return {hexc(0xfff8ee), false};
}

void nameTab(int stage, const std::string& name, unsigned colHex, float x, float y)
{
    const int era = boxStyle(stage);
    float w = textW(name, 24) + 40, h = 40;
    Color col = hexc(colHex);
    if (era == 0) {
        DrawRectangleRec(R(x, y, w, h), hexc(0x101216));
        DrawRectangleLinesEx(R(x + 4, y + 4, w - 8, h - 8), std::max(1.0f, 2 * S), hexc(0xebebe1));
        text(name, x + w / 2, y + h / 2, 24, hexc(0xebebe1), false, true);
    } else if (era == 1) {
        DrawRectangleRec(R(x, y, w, h), hexc(0x0f0d1a));
        DrawRectangleRec(R(x + 5, y + 5, w - 10, h - 10), col);
        text(name, x + w / 2, y + h / 2, 24, WHITE, true, true);
    } else if (era == 2) {
        DrawRectangleRounded(R(x, y, w, h), 0.4f, 6, hexc(0xd8e0f0));
        DrawRectangleRounded(R(x + 4, y + 4, w - 8, h - 8), 0.4f, 6, col);
        text(name, x + w / 2, y + h / 2, 24, WHITE, true, true);
    } else {
        DrawRectangleRounded(R(x, y, w, h), 1.0f, 12, fade(col, 0.92f));
        text(name, x + w / 2, y + h / 2, 24, WHITE, true, true);
    }
}

// Black slanted bars sweeping across the screen. cover 0..1 per bar; fromRight alternates.
void drawBars(float p, bool in)
{
    const int N = 9;
    const float bh = 720.0f / N, slant = 90;
    for (int i = 0; i < N; ++i) {
        float k = easeOut((p * 1.5f) - i * 0.055f);
        float cover = in ? k : 1 - k;
        if (cover <= 0) continue;
        float y0 = i * bh - 1, y1 = (i + 1) * bh + 1;
        float span = (1280 + slant) * cover;
        if (i % 2 == 0) {   // from the right
            float x = 1280 + slant - span;
            quad({x, y0}, {1280 + slant, y0}, {1280 + slant, y1}, {x - slant, y1}, BLACK);
        } else {            // from the left
            float x = -slant + span;
            quad({-slant, y0}, {x + slant, y0}, {x, y1}, {-slant, y1}, BLACK);
        }
    }
}

}  // namespace

// ------------------------------------------------------------------ script file
std::string Cutscene::bossId(int kind)
{
    return kind == 2 ? "school_bell" : kind == 4 ? "last_train" : kind == 6 ? "forgetting" : "";
}

void Cutscene::unload()
{
    if (hourglass_.id) UnloadTexture(hourglass_);
    hourglass_ = Texture2D{};
}

void Cutscene::load(const std::string& path)
{
    scenes_.clear();
    std::ifstream f(path);
    if (!f) { TraceLog(LOG_WARNING, "CUTSCENE: no %s", path.c_str()); return; }
    std::string raw;
    Scene* cur = nullptr;
    while (std::getline(f, raw)) {
        std::string s = trim(raw);
        if (s.empty() || s[0] == '#') continue;
        if (s[0] == '[') {
            size_t close = s.find(']');
            std::stringstream ss(s.substr(1, close == std::string::npos ? std::string::npos : close - 1));
            std::string name, opt;
            ss >> name;
            Scene sc;
            sc.name = name;
            while (ss >> opt)
                if (opt.rfind("vs=", 0) == 0) sc.vs = opt.substr(3);
            scenes_[name] = sc;
            cur = &scenes_[name];
            continue;
        }
        if (!cur) continue;
        size_t colon = s.find(':');
        Line ln;
        if (colon != std::string::npos && colon < 16) {
            ln.speaker = trim(s.substr(0, colon));
            std::transform(ln.speaker.begin(), ln.speaker.end(), ln.speaker.begin(), ::tolower);
            ln.text = trim(s.substr(colon + 1));
        } else {
            ln.speaker = "narrator";
            ln.text = s;
        }
        if (!ln.text.empty()) cur->lines.push_back(ln);
    }
    TraceLog(LOG_INFO, "CUTSCENE: loaded %d scenes", (int)scenes_.size());
}

// ------------------------------------------------------------------ control
bool Cutscene::play(Game& g, const std::string& name)
{
    auto it = scenes_.find(name);
    if (it == scenes_.end() || it->second.lines.empty() || played_.count(name)) return false;
    played_.insert(name);
    if (g.opt.autoplay && !g.opt.cutscenes) return false;   // automated tests skip every scene
    if (cur_) queue_.push_back(&it->second);
    else begin(g, &it->second);
    return true;
}

void Cutscene::then(std::function<void()> fn)
{
    if (!cur_) fn();
    else then_.push_back(std::move(fn));
}

void Cutscene::begin(Game& g, const Scene* s)
{
    bool first = cur_ == nullptr;
    cur_ = s;
    t_ = phaseT_ = 0;
    line_ = 0;
    holdT_ = 0;
    armed_ = false;
    shake_ = 0;
    slammed_ = false;
    ticks_ = 0;
    rightWho_.clear();
    leftIn_ = rightIn_ = 0;
    for (const auto& l : s->lines)
        if (l.speaker == "sam" || isBoss(l.speaker)) { rightWho_ = l.speaker; break; }
    if (first) {
        stashBanner_ = g.bannerT;   // the banner waits behind the scene and comes back after
        g.bannerT = 0;
        g.audio.setMusicDuck(0.45f);
    }
    if (!s->vs.empty()) {
        phase_ = PH_FLASH;
        g.audio.play("encounter");
    } else {
        phase_ = PH_TALK;
        g.audio.play("select", 0.6f);
        startLine(g);
    }
}

void Cutscene::finish(Game& g)
{
    vo::stop();   // akshat
    if (!queue_.empty()) {
        const Scene* next = queue_.front();
        queue_.pop_front();
        begin(g, next);
        return;
    }
    cur_ = nullptr;
    g.audio.setMusicDuck(1.0f);
    g.bannerT = stashBanner_;
    auto fns = std::move(then_);
    then_.clear();
    for (auto& fn : fns) fn();
}

void Cutscene::startLine(Game& g)
{
    g_font = fontFor(g.era);
    page_ = 0;
    shown_ = 0;
    doneT_ = 0;
    lineT_ = 0;
    blips_ = 0;
    pages_.clear();
    const Line& ln = cur_->lines[line_];
    vo::play(ln.speaker, ln.text);   // akshat: voiceover for this line, if it was generated
    if (ln.speaker == "sam" || isBoss(ln.speaker)) {
        if (rightWho_ != ln.speaker) rightIn_ = 0;
        rightWho_ = ln.speaker;
    }
    // word wrap to the box width, then group into pages
    const float maxW = BOX_W - TEXT_PAD_X * 2 - 30;
    std::vector<std::string> lines;
    std::stringstream ss(ln.text);
    std::string word, cur;
    while (ss >> word) {
        std::string tryS = cur.empty() ? word : cur + " " + word;
        if (!cur.empty() && textW(tryS, TEXT_SIZE) > maxW) { lines.push_back(cur); cur = word; }
        else cur = tryS;
    }
    if (!cur.empty()) lines.push_back(cur);
    for (size_t i = 0; i < lines.size(); i += LINES_PER_PAGE)
        pages_.emplace_back(lines.begin() + i, lines.begin() + std::min(lines.size(), i + LINES_PER_PAGE));
    if (pages_.empty()) pages_.push_back({""});
}

int Cutscene::pageChars() const
{
    int n = 0;
    for (const auto& l : pages_[page_]) n += (int)l.size();
    return n;
}

void Cutscene::advance(Game& g)
{
    if (shown_ < pageChars()) { shown_ = (float)pageChars(); return; }   // first press: finish the page
    g.audio.play("advance");
    if (page_ + 1 < (int)pages_.size()) {
        ++page_;
        shown_ = 0;
        doneT_ = 0;
        blips_ = 0;
        return;
    }
    if (line_ + 1 < (int)cur_->lines.size()) {
        ++line_;
        startLine(g);
        return;
    }
    if (!cur_->vs.empty()) { phase_ = PH_OUTRO; phaseT_ = 0; g.audio.play("swipe"); }
    else finish(g);
}

void Cutscene::update(Game& g, float dt, bool firstTick)
{
    if (!cur_) return;
    t_ += dt;
    phaseT_ += dt;
    leftIn_ = std::min(1.0f, leftIn_ + dt / 0.35f);
    rightIn_ = std::min(1.0f, rightIn_ + dt / 0.35f);
    shake_ = std::max(0.0f, shake_ - dt * 2.5f);
    const bool autoAdv = g.opt.autoplay && g.opt.cutscenes;

    // ---- input: any player's jump/attack, Enter, Space or pad A advances; hold to skip; Esc skips all
    bool press = false, held = false;
    if (firstTick && !g.opt.autoplay) {
        if (g.input.pausePressed()) {
            queue_.clear();
            g.audio.play("advance");
            finish(g);
            return;
        }
        for (int p = 0; p < 2; ++p) {
            PlayerInput in = g.input.read(p);
            press |= in.jumpPressed || in.attackPressed;
            held |= in.jumpHeld;
        }
        press |= IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE);
        held |= IsKeyDown(KEY_ENTER) || IsKeyDown(KEY_SPACE);
        if (t_ < GRACE) press = false;
        if (press) armed_ = true;
    }
    if (firstTick && !g.opt.autoplay) {
        holdT_ = (held && armed_) ? holdT_ + GetFrameTime() : 0;
        if (holdT_ > HOLD_SKIP) {
            g.audio.play("advance");
            finish(g);
            return;
        }
    }

    // ---- the wild-battle intro
    if (phase_ == PH_FLASH) {
        // the bell clangs high, the train's whistle-dong sits low, the hourglass lowest of all
        const float dongPitch = cur_->vs == "school_bell" ? 1.25f : cur_->vs == "last_train" ? 0.85f : 0.7f;
        float prev = phaseT_ - dt;
        if (prev < FLASH_END && phaseT_ >= FLASH_END) g.audio.play("swipe");
        if (prev < SLAM_AT && phaseT_ >= SLAM_AT) {
            slammed_ = true;
            shake_ = 1;
            g.audio.play("slam");
            g.audio.play("dong", 1.0f, dongPitch);
        }
        if (phaseT_ > BANDS_END && (int)((phaseT_ - BANDS_END) / 0.45f) > ticks_) {   // the clock ticks while you stare
            ++ticks_;
            g.audio.play("tick", 0.7f, ticks_ % 2 ? 1.0f : 0.85f);
        }
        if (press && phaseT_ > SLAM_AT) phaseT_ = TALK_AT;
        if (phaseT_ >= TALK_AT) { phase_ = PH_TALK; startLine(g); }
        return;
    }
    if (phase_ == PH_OUTRO) {
        if (phaseT_ >= OUTRO_LEN) finish(g);
        return;
    }

    // ---- typing
    lineT_ += dt;
    const Voice& v = voice(cur_->lines[line_].speaker);
    int total = pageChars();
    if (shown_ < total) {
        int before = (int)shown_;
        shown_ = std::min((float)total, shown_ + v.cps * dt);
        int after = (int)shown_;
        // a voice blip every second character (spaces stay quiet)
        int idx = 0;
        bool blip = false;
        for (const auto& l : pages_[page_]) {
            for (char c : l) {
                if (idx >= before && idx < after && c != ' ' && (++blips_ % 2 == 1)) blip = true;
                ++idx;
            }
        }
        if (blip) g.audio.play("blip", 0.55f, v.pitch * (0.96f + 0.08f * (float)(std::rand() % 100) / 100.0f));
    } else {
        doneT_ += dt;
    }
    if (press) advance(g);
    else if (autoAdv && shown_ >= total && doneT_ > AUTO_WAIT) advance(g);
}

// ------------------------------------------------------------------ drawing
void Cutscene::draw(Game& g)
{
    if (!cur_) return;
    setup(g);
    g_font = fontFor(g.era);
    g_hourglass = &hourglass_;
    const int era = g.era;
    const bool vs = !cur_->vs.empty();
    float sx = shake_ > 0 ? std::sin(t_ * 70) * 14 * shake_ : 0, sy = shake_ > 0 ? std::cos(t_ * 53) * 10 * shake_ : 0;
    const std::string speaker = (phase_ == PH_TALK) ? cur_->lines[line_].speaker : "";
    const bool typing = phase_ == PH_TALK && shown_ < pageChars();
    const bool hurt = cur_->name.find("defeat") != std::string::npos;

    // ---- encounter: flashes, then bars, then the VS screen
    bool vsScreen = vs && (phase_ == PH_TALK || (phase_ == PH_FLASH && phaseT_ >= WIPE_END));
    if (vs && phase_ == PH_FLASH && phaseT_ < FLASH_END) {
        if (std::fmod(phaseT_, 0.2f) < 0.1f) DrawRectangleRec(R(0, 0, 1280, 720), fade(WHITE, 0.9f));
        return;
    }
    if (vs && phase_ == PH_FLASH && phaseT_ < WIPE_END) {
        drawBars((phaseT_ - FLASH_END) / (WIPE_END - FLASH_END), true);
        return;
    }
    if (vs && phase_ == PH_OUTRO) {
        drawBars(phaseT_ / OUTRO_LEN, false);
        return;
    }

    if (vsScreen) {
        float bt = phase_ == PH_FLASH ? phaseT_ - WIPE_END : 10.0f;
        float k = easeOut(bt / (BANDS_END - WIPE_END));
        DrawRectangleRec(R(0, 0, 1280, 720), BLACK);
        // top band: the boss, sliding in from the right
        Color bandB = hexc(bandColour(cur_->vs));
        float ox = (1 - k) * 1400 + sx;
        quad({120 + ox, 70 + sy}, {1400 + ox, 70 + sy}, {1400 + ox, 290 + sy}, {60 + ox, 290 + sy}, bandB);
        for (int i = 0; i < 7; ++i) {   // speed lines
            float ly = 86 + i * 30, lx = std::fmod(t_ * 900 + i * 337, 1500.0f) - 100;
            DrawRectangleRec(R(lx + ox, ly + sy, 160 + (i % 3) * 60, 4), fade(WHITE, 0.18f));
        }
        bool bossTalk = speaker == cur_->vs;
        float bob = bossTalk && typing ? std::sin(t_ * 10) * 6 : 0;
        portrait(g, cur_->vs, 1010 + ox, 300 + sy + bob, 270, true, WHITE, bossTalk, t_, false);
        // bottom band: Gran and Sam, sliding in from the left
        Color bandH = hexc(era <= 1 ? 0x4a4a52 : 0x2f6fd0);
        float hx = -(1 - k) * 1400 + sx;
        quad({-120 + hx, 310 + sy}, {1220 + hx, 310 + sy}, {1160 + hx, 520 + sy}, {-120 + hx, 520 + sy}, bandH);
        for (int i = 0; i < 7; ++i) {
            float ly = 326 + i * 28, lx = 1400 - std::fmod(t_ * 900 + i * 291, 1500.0f);
            DrawRectangleRec(R(lx + hx, ly + sy, 160 + (i % 3) * 60, 4), fade(WHITE, 0.18f));
        }
        const char* heroes[2] = {"gran", "sam"};
        for (int p = 0; p < 2; ++p) {
            bool talk = speaker == heroes[p];
            float b = talk && typing ? std::sin(t_ * 10) * 6 : 0;
            Color tint = (talk || speaker.empty() || isBoss(speaker)) ? WHITE : Color{180, 180, 190, 255};
            portrait(g, heroes[p], 200 + p * 170 + hx, 512 + sy + b, 200, p == 1, tint, talk, t_ + p, false);
        }
        text(std::string(T("p1_name")) + " & " + T("p2_name"), 1010 + hx, 380 + sy, 44, WHITE, true, true, 1, fade(BLACK, 0.8f), ui::PIXEL);
        // the name slams in
        if (slammed_) {
            float st = phase_ == PH_FLASH ? phaseT_ - SLAM_AT : 10.0f;
            float sc = 1.0f + 1.6f * (1 - easeOut(st / 0.15f));
            std::string nm = speakerName(cur_->vs);
            text(nm, 470 + sx, 180 + sy, 60, hexc(0xfff4d8), true, true, sc, fade(BLACK, 0.85f), ui::PIXEL);
            text("VS", 640 + sx * 1.5f, 300 + sy, 76, hexc(0xf7d73a), true, true, sc, hexc(0x2a1606), ui::PIXEL);
        }
        if (phase_ == PH_FLASH) return;
    } else {
        // ---- ordinary scene: dim the world and bring in the portraits
        float a = std::min(1.0f, t_ / 0.3f);
        DrawRectangleRec(R(0, 0, 1280, 720), fade(BLACK, 0.35f * a));
        DrawRectangleGradientV((int)OX, (int)(OY + 300 * S), (int)(1280 * S), (int)(420 * S), fade(BLACK, 0), fade(BLACK, 0.45f * a));
        bool hasGran = false;
        for (const auto& l : cur_->lines) if (l.speaker == "gran") hasGran = true;
        if (hasGran) {
            bool talk = speaker == "gran";
            float bob = talk && typing ? std::sin(lineT_ * 10) * 6 : 0;
            float x = LEFT_X - (1 - easeOut(leftIn_)) * 500;
            Color tint = talk ? WHITE : Color{150, 150, 160, 255};
            portrait(g, "gran", x, PORTRAIT_BOTTOM + bob + (talk ? 0 : 8), PORTRAIT_H, false, tint, talk, lineT_, false);
        }
        if (!rightWho_.empty()) {
            bool talk = speaker == rightWho_;
            float bob = talk && typing ? std::sin(lineT_ * 10 + 1) * 6 : 0;
            float x = RIGHT_X + (1 - easeOut(rightIn_)) * 500;
            Color tint = talk ? WHITE : Color{150, 150, 160, 255};
            float h = isBoss(rightWho_) ? 330 : PORTRAIT_H;
            portrait(g, rightWho_, x, PORTRAIT_BOTTOM + bob + (talk ? 0 : 8), h, true, tint, talk, lineT_,
                     hurt && isBoss(rightWho_));
        }
    }

    // ---- the box
    float boxIn = easeOut(phase_ == PH_TALK ? (vs ? phaseT_ / 0.25f : t_ / 0.25f) : 0);
    float by = BOX_Y + (1 - boxIn) * 220;
    BoxStyle st = drawBox(era, BOX_X, by, BOX_W, BOX_H);
    const Voice& v = voice(speaker);
    std::string nm = speakerName(speaker);
    if (!nm.empty()) {
        bool right = speaker == "sam" || isBoss(speaker);
        float w = textW(nm, 24) + 40;
        nameTab(era, nm, v.col, right ? BOX_X + BOX_W - 40 - w : BOX_X + 40, by - 30);
    }
    // typewriter text
    int left = (int)shown_;
    const bool narr = speaker == "narrator";
    Color tc = narr ? fade(st.text, 0.8f) : st.text;
    for (size_t i = 0; i < pages_[page_].size() && left > 0; ++i) {
        const std::string& l = pages_[page_][i];
        int n = std::min(left, (int)l.size());
        text(l.substr(0, n), BOX_X + TEXT_PAD_X, by + TEXT_PAD_Y + 6 + i * LINE_H, TEXT_SIZE, tc, st.shadow);
        left -= n;
    }
    // blinking down arrow when the page is done
    if (!typing && std::fmod(g.time, 0.7f) < 0.45f) {
        float ax = BOX_X + BOX_W - 50, ay = by + BOX_H - 40 + std::sin(g.time * 6) * 3;
        quad({ax - 11, ay}, {ax + 11, ay}, {ax + 11, ay}, {ax, ay + 13}, st.text);
    }
    // skip hint + hold progress
    {
        std::vector<ui::Prompt> ps = {{{"Space"}, {ui::PAD_A}, T("cutscene_hold_skip")}, {{"Esc"}, {ui::PAD_START}, T("cutscene_skip_all")}};
        // under the box, on the right
        const float ph = 20, right = BOX_X + BOX_W - 8, py = by + BOX_H + 6;
        float tw = 0;
        for (auto& p : ps) tw += ui::promptW(p, ph) + ph * 1.1f;
        tw -= ph * 1.1f;
        ui::promptRow(ps, right, py, ph, fade(WHITE, 0.75f), ui::RIGHT, boxIn);
        if (holdT_ > 0) ui::rrect({right - tw, py + ph + 2, tw * std::min(1.0f, holdT_ / HOLD_SKIP), 2}, 1, ui::col::amber);
    }
}
