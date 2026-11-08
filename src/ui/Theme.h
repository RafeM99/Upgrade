// Theme.h (akshat): the shared look of every screen - fonts, type scale, colours per stage,
// panels, buttons, key / button glyphs and easing. Hud, Story and Cutscene all draw through it.
//
// Space: every screen is laid out on a 1280 x 720 page ("UI units") that is scaled into a
// rectangle of the window (the game image for the HUD, the 16:9 fit of the window for menus).
// Call ui::setFrame() once before drawing; X/Y/R turn UI units into window pixels.
//
// Fonts (assets/fonts, SIL Open Font License):
//   ui::SERIF         Lora - menus, story text, subtitles, dialogue in the smooth stages
//   ui::SERIF_ITALIC  Lora Italic - asides, descriptions, section heads
//   ui::HAND          Caveat - handwriting: photo captions, dates, little notes, chosen values
//   ui::PIXEL         Silkscreen - the in-game HUD and the pixel stages (PIXEL_BOLD: its bold)
//   ui::BODY is an alias of SERIF (older code).
// The smooth fonts are rasterised at the exact physical pixel size they are drawn at (Retina
// aware). Silkscreen is drawn on its own pixel grid: one font pixel is always a whole number of
// screen pixels, so the size snaps to steps of ~10 UI units (20 and 31 are the usual ones).
#pragma once
#include "raylib.h"

#include <string>
#include <vector>

namespace ui {

enum FontId { SERIF = 0, SERIF_ITALIC = 1, HAND = 2, PIXEL = 3, PIXEL_BOLD = 4, FONT_COUNT = 5, BODY = SERIF };
inline bool isPixelFont(int f) { return f == PIXEL || f == PIXEL_BOLD; }
enum Align { LEFT = 0, CENTER = 1, RIGHT = 2 };

// ---- type scale (UI units, line-box size as raylib measures fonts)
namespace type {
constexpr float display = 110;  // the logo
constexpr float h1 = 56;        // screen titles
constexpr float h2 = 40;        // panel titles, boss names in a VS
constexpr float h3 = 34;        // menu items
constexpr float body = 31;      // story lines, dialogue, subtitles
constexpr float label = 25;     // descriptions, prompts (never smaller than this for reading)
constexpr float hand = 38;      // handwritten notes
constexpr float pixel = 20.5f;  // Silkscreen, one step (8 px em at 720p)
}  // namespace type

// ---- spacing (UI units)
namespace space {
constexpr float xs = 4, sm = 8, md = 12, lg = 20, xl = 32, xxl = 48;
}

// ---- colours shared by every screen
namespace col {
extern const Color ink, ink2, cream, creamDim, mute, amber, gold, gran, sam, good, danger, lilac;
// paper: the scrapbook look of the menus and the story chrome
extern const Color paper, paperDark, paperInk, paperInk2, paperRule, brick, highlight, tape, granInk, samInk;
}

// ---- frame
void setFrame(Rectangle windowRect);    // the window rectangle the 1280x720 page maps onto
Rectangle menuRect();                   // the 16:9 fit of the whole window (menus, story)
float S();                              // window pixels per UI unit
float dpi();                            // physical pixels per window pixel (2 on Retina)
float X(float x);                       // UI -> window, snapped to a physical pixel
float Y(float y);
float L(float len);                     // a UI length in window pixels
Rectangle R(float x, float y, float w, float h);
float hair();                           // one physical pixel, in UI units
float now();                            // seconds (real time) for animations

// ---- text (x, y: top-left of the line box, in UI units)
struct TextStyle {
    float size = type::body;
    int font = BODY;
    bool bold = false;                  // faux bold: drawn twice, a hair apart
    float track = 0;                    // extra letter spacing, as a fraction of size
    int align = LEFT;
    Color color = WHITE;
    Color shadow = BLANK;               // a drop shadow when alpha > 0
    float shadowOff = 2;                // UI units
    Color outline = BLANK;              // an outline when alpha > 0 (big titles)
    float outlineW = 2;
};
float textW(const std::string& s, float size, int font = BODY, float track = 0, bool bold = false);
float textW(const std::string& s, const TextStyle& st);
void text(const std::string& s, float x, float y, const TextStyle& st);
void text(const std::string& s, float x, float y, float size, Color c, int align = LEFT, int font = BODY, bool bold = false);
// y is the visual middle of capital letters
void textMid(const std::string& s, float x, float midY, const TextStyle& st);
// The atlas for drawing at sizeUI by hand (e.g. inside a rotated matrix); drawSize: the size
// to pass to DrawTextEx, in window pixels.
const Font& fontAt(float sizeUI, int font, float* drawSize);
std::vector<std::string> wrap(const std::string& s, float size, float maxW, int font = BODY, float track = 0);
std::string upper(std::string s);

// ---- easing and animation
float clamp01(float v);
float easeOutCubic(float k);
float easeInOutCubic(float k);
float easeOutBack(float k);
float approach(float cur, float target, float rate, float dt);   // exponential smoothing
// A smoothed value kept between frames, by a key (e.g. menu highlight position).
float anim(int key, float target, float rate = 14);
void animSet(int key, float value);
Color fade(Color c, float a);
Color mix(Color a, Color b, float k);
Color hex(unsigned rgb, unsigned char a = 255);

// ---- shapes (UI units)
void rect(Rectangle r, Color c);
void rrect(Rectangle r, float radius, Color c);
void roundLines(Rectangle r, float radius, float thick, Color c);
void gradV(Rectangle r, Color top, Color bottom);
void gradH(Rectangle r, Color left, Color right);
void softShadow(Rectangle r, float radius, float spread, float alpha);
void vignette(Rectangle r, float strength);        // dark edges over r
void tri(Vector2 a, Vector2 b, Vector2 c, Color col);   // any winding

// ---- stage skins: the HUD and dialogue of each graphics stage
struct Skin {
    int era = 0;
    bool pixel = true;                 // square corners, crisp borders, Pixelify text
    int font = PIXEL;
    float radius = 0;
    Color bg, bg2;                     // panel fill (top / bottom when a gradient)
    Color line, lineHi;                // border and highlight
    Color text, dim;                   // text and secondary text
    Color accent;                      // memory bar, highlights
    Color accent2;                     // memory bar lower half / secondary
    Color trough;                      // empty bar
    Color p[2];                        // Gran, Sam
    Color danger, boss;
};
const Skin& skin(int era);
const Skin& menuSkin();                 // menus: one look on every stage
void panel(Rectangle r, const Skin& k, float alpha = 1);
void chip(Rectangle r, const Skin& k, Color c, float alpha = 1);   // a small pill / tag

// ---- glyphs: a keyboard key or a gamepad button, h tall at (x, y top). Returns the width.
enum PadGlyph { PAD_A, PAD_B, PAD_X, PAD_Y, PAD_START, PAD_SELECT, PAD_DPAD, PAD_STICK, PAD_LB, PAD_RB };
float keycap(const std::string& label, float x, float y, float h, float alpha = 1, bool dark = false);
float keycapW(const std::string& label, float h);
float padButton(int glyph, float x, float y, float h, float alpha = 1);
// A control prompt: glyphs then a label, e.g. "[Enter] [A]  Select". Returns the width.
struct Prompt { std::vector<std::string> keys; std::vector<int> pads; std::string label; };
float promptW(const Prompt& p, float h);
float prompt(const Prompt& p, float x, float y, float h, Color labelCol, float alpha = 1);
// A row of prompts, right-aligned to x (align RIGHT) or centred on it (align CENTER).
void promptRow(const std::vector<Prompt>& ps, float x, float y, float h, Color labelCol, int align, float alpha = 1);
bool anyPad();                          // a gamepad is connected: show its buttons

// ---- menu pieces
// A selectable row: a soft highlight that slides between rows (key: the menu's animation id).
void selectionBar(int key, float x, float targetY, float w, float h, Color c, float alpha = 1);
void arrows(float cx, float midY, float halfW, float size, Color c, float pulse);   // < value >
void toggle(float x, float midY, bool on, Color onCol, Color offCol, float alpha = 1);

// ---- the scrapbook look
void paper(Rectangle r, Color base = BLANK, float alpha = 1, float shadow = 1);   // a sheet with grain and a shadow
void tape(float cx, float cy, float w, float angleDeg, float alpha = 1);           // a strip of tape
void highlighter(Rectangle r, Color c, float alpha = 1);                         // a marker stroke behind text
void inkLine(float x0, float y0, float x1, float y1, float thick, Color c);       // a slightly wobbly pen line
void checkbox(float x, float midY, float size, bool on, Color ink, float alpha = 1);
void beginRotate(float cx, float cy, float deg);   // everything until endRotate turns about (cx, cy)
void endRotate();
void grain(Rectangle r, float alpha);              // paper grain over a rectangle

void unload();

}  // namespace ui
