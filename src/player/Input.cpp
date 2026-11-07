// Owner: Core player (area 2). Starter code from the skeleton: replace freely.
#include "player/Input.h"
#include <cmath>
#include "raylib.h"

namespace {
struct PlayerKeys {
    int left, right, up, down, jump, jumpAlt, attack, ability;
    int gamepad;
};

// Change bindings here (brief section 6).
const PlayerKeys kKeys[2] = {
    {KEY_A, KEY_D, KEY_W, KEY_S, KEY_SPACE, KEY_NULL, KEY_F, KEY_G, 0},
    {KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN, KEY_RIGHT_CONTROL, KEY_ENTER, KEY_RIGHT_SHIFT, KEY_SLASH, 1},
};

bool down(int key) { return key != KEY_NULL && IsKeyDown(key); }
bool pressed(int key) { return key != KEY_NULL && IsKeyPressed(key); }
}

void Input::poll() {
    ++frame;
    for (int p = 0; p < 2; ++p) {
        const PlayerKeys& k = kKeys[p];
        float x = 0.0f, y = 0.0f;
        if (down(k.left)) x -= 1.0f;
        if (down(k.right)) x += 1.0f;
        if (down(k.up)) y += 1.0f;
        if (down(k.down)) y -= 1.0f;
        bool jumpHeld = down(k.jump) || down(k.jumpAlt);
        bool jumpPressed = pressed(k.jump) || pressed(k.jumpAlt);
        bool attack = pressed(k.attack);
        bool ability = pressed(k.ability);
        bool any = jumpPressed || attack || ability || pressed(k.left) || pressed(k.right) ||
                   pressed(k.up) || pressed(k.down);

        int pad = k.gamepad;
        if (IsGamepadAvailable(pad)) {
            float ax = GetGamepadAxisMovement(pad, GAMEPAD_AXIS_LEFT_X);
            if (std::fabs(ax) > 0.25f) x = ax;
            if (IsGamepadButtonDown(pad, GAMEPAD_BUTTON_LEFT_FACE_LEFT)) x = -1.0f;
            if (IsGamepadButtonDown(pad, GAMEPAD_BUTTON_LEFT_FACE_RIGHT)) x = 1.0f;
            jumpHeld = jumpHeld || IsGamepadButtonDown(pad, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
            bool padJump = IsGamepadButtonPressed(pad, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
            bool padAttack = IsGamepadButtonPressed(pad, GAMEPAD_BUTTON_RIGHT_FACE_LEFT);
            bool padAbility = IsGamepadButtonPressed(pad, GAMEPAD_BUTTON_RIGHT_FACE_UP);
            jumpPressed = jumpPressed || padJump;
            attack = attack || padAttack;
            ability = ability || padAbility;
            any = any || padJump || padAttack || padAbility ||
                  IsGamepadButtonPressed(pad, GAMEPAD_BUTTON_MIDDLE_RIGHT);
        }

        if (autoplay) {
            long t = frame + p * 20;
            x = 1.0f;
            jumpHeld = t % 50 < 18;
            jumpPressed = t % 50 == 0;
            if (p == 1 && frame == 30) any = true;
        }

        InputState& s = state[p];
        s.moveX = x < -1.0f ? -1.0f : (x > 1.0f ? 1.0f : x);
        s.moveY = y;
        s.jumpHeld = jumpHeld;
        s.jumpPressed = s.jumpPressed || jumpPressed;
        s.attackPressed = s.attackPressed || attack;
        s.abilityPressed = s.abilityPressed || ability;
        s.anyPressed = s.anyPressed || any;
    }
}

InputState Input::take(int player) {
    InputState out = state[player];
    state[player].jumpPressed = false;
    state[player].attackPressed = false;
    state[player].abilityPressed = false;
    state[player].anyPressed = false;
    return out;
}
