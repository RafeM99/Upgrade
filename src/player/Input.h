// Input.h: two-player input. Keyboard bindings come from assets/config.ini [input],
// so they are easy to change. Player 1 also uses gamepad 0, Player 2 gamepad 1.
#pragma once
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
    PlayerInput read(int player) const;
    bool pausePressed() const;
    bool menuUp() const;
    bool menuDown() const;
    bool menuSelect() const;
    bool menuLeft() const;
    bool menuRight() const;
    bool menuBack() const;     // Esc / Backspace / gamepad B

private:
    std::vector<int> keys_[2][7];   // left right up down jump attack ability
    std::vector<int> pause_;
};
