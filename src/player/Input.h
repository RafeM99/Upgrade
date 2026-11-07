// Input.h: two-player input. Keyboard bindings come from assets/config.ini [input],
// so they are easy to change.
//
// Gamepads (any slot raylib finds them in): the first connected pad plays Gran, the second
// Sam. With [input] first_pad_player = 2 a single pad plays Sam instead (Gran on the keyboard).
// Each player also keeps their keyboard keys. Layout (Xbox names; the same positions on
// PlayStation and Switch pads):
//   left stick / d-pad  move (down + B: second ability; down in the air: ground pound)
//   A                   jump (hold to glide)          X or LB   attack
//   B or RB             ability                       Y         second ability
//   Start               pause                         B         back / resume in the pause menu
// Menus take the d-pad or the left stick. An optional assets/gamecontrollerdb.txt (SDL format)
// adds mappings for pads raylib does not know.
#pragma once
#include <string>
#include <vector>

struct PlayerInput {
    bool left = false, right = false, up = false, down = false;
    bool jumpHeld = false, jumpPressed = false;
    bool attackPressed = false, abilityPressed = false;
    bool any = false;   // any of this player's buttons went down this frame
};

class InputMap {
public:
    void load();
    void beginFrame();                 // once per frame, before any read: stick edges for menus
    PlayerInput read(int player) const;
    int padFor(int player) const;      // the gamepad slot this player uses, or -1
    int padCount() const;              // usable gamepads connected right now
    // For the title screen: every controller plugged in, its name, and whether its buttons are
    // known (a controller with no mapping is seen but every button reads as up).
    struct PadInfo { int slot; std::string name; bool usable; int player; };
    std::vector<PadInfo> pads() const;
    bool pausePressed() const;
    bool menuUp() const;
    bool menuDown() const;
    bool menuSelect() const;
    bool menuLeft() const;
    bool menuRight() const;
    bool menuBack() const;     // Esc / Backspace / gamepad B or Start (any pad)

private:
    std::vector<int> keys_[2][7];   // left right up down jump attack ability
    std::vector<int> pause_;
    int firstPadPlayer_ = 1;
    float stickY_[4] = {0, 0, 0, 0}, prevStickY_[4] = {0, 0, 0, 0};
};
