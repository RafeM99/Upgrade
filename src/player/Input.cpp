#include "player/Input.h"
#include "core/Config.h"
#include "raylib.h"

#include <cstring>
#include <sstream>
#include <string>

namespace {

struct KeyName { const char* name; int key; };
const KeyName KEYS[] = {
    {"SPACE", KEY_SPACE}, {"ENTER", KEY_ENTER}, {"ESCAPE", KEY_ESCAPE}, {"TAB", KEY_TAB}, {"BACKSPACE", KEY_BACKSPACE},
    {"LEFT", KEY_LEFT}, {"RIGHT", KEY_RIGHT}, {"UP", KEY_UP}, {"DOWN", KEY_DOWN},
    {"LEFT_SHIFT", KEY_LEFT_SHIFT}, {"RIGHT_SHIFT", KEY_RIGHT_SHIFT}, {"LEFT_CONTROL", KEY_LEFT_CONTROL},
    {"RIGHT_CONTROL", KEY_RIGHT_CONTROL}, {"LEFT_ALT", KEY_LEFT_ALT}, {"RIGHT_ALT", KEY_RIGHT_ALT},
    {"SLASH", KEY_SLASH}, {"PERIOD", KEY_PERIOD}, {"COMMA", KEY_COMMA}, {"SEMICOLON", KEY_SEMICOLON},
    {"APOSTROPHE", KEY_APOSTROPHE}, {"KP_0", KEY_KP_0}, {"KP_1", KEY_KP_1}, {"KP_2", KEY_KP_2}, {"KP_3", KEY_KP_3},
    {"KP_ENTER", KEY_KP_ENTER}, {"RIGHT_SUPER", KEY_RIGHT_SUPER},
};

int keyFromName(const std::string& n)
{
    if (n.size() == 1 && n[0] >= 'A' && n[0] <= 'Z') return KEY_A + (n[0] - 'A');
    if (n.size() == 1 && n[0] >= '0' && n[0] <= '9') return KEY_ZERO + (n[0] - '0');
    for (const auto& k : KEYS)
        if (n == k.name) return k.key;
    return 0;
}

std::vector<int> parseKeys(const std::string& list)
{
    std::vector<int> out;
    std::stringstream ss(list);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        while (!tok.empty() && tok.front() == ' ') tok.erase(tok.begin());
        while (!tok.empty() && tok.back() == ' ') tok.pop_back();
        int k = keyFromName(tok);
        if (k) out.push_back(k);
    }
    return out;
}

bool down(const std::vector<int>& ks) { for (int k : ks) if (IsKeyDown(k)) return true; return false; }
bool pressed(const std::vector<int>& ks) { for (int k : ks) if (IsKeyPressed(k)) return true; return false; }

}  // namespace

void InputMap::load()
{
    const char* names[7] = {"left", "right", "up", "down", "jump", "attack", "ability"};
    const char* defaults[2][7] = {{"A", "D", "W", "S", "SPACE", "F", "G"},
                                  {"LEFT", "RIGHT", "UP", "DOWN", "RIGHT_CONTROL,ENTER", "RIGHT_SHIFT", "SLASH"}};
    for (int p = 0; p < 2; ++p)
        for (int a = 0; a < 7; ++a)
            keys_[p][a] = parseKeys(config().s("input.p" + std::to_string(p + 1) + "_" + names[a], defaults[p][a]));
    pause_ = parseKeys(config().s("input.pause", "ESCAPE,P"));
}

PlayerInput InputMap::read(int p) const
{
    PlayerInput in;
    const auto& k = keys_[p];
    in.left = down(k[0]);
    in.right = down(k[1]);
    in.up = down(k[2]);
    in.down = down(k[3]);
    in.jumpHeld = down(k[4]);
    in.jumpPressed = pressed(k[4]);
    in.attackPressed = pressed(k[5]);
    in.abilityPressed = pressed(k[6]);
    in.any = in.jumpPressed || in.attackPressed || in.abilityPressed || pressed(k[0]) || pressed(k[1]);
    if (IsGamepadAvailable(p)) {
        float ax = GetGamepadAxisMovement(p, GAMEPAD_AXIS_LEFT_X), ay = GetGamepadAxisMovement(p, GAMEPAD_AXIS_LEFT_Y);
        in.left |= ax < -0.35f || IsGamepadButtonDown(p, GAMEPAD_BUTTON_LEFT_FACE_LEFT);
        in.right |= ax > 0.35f || IsGamepadButtonDown(p, GAMEPAD_BUTTON_LEFT_FACE_RIGHT);
        in.up |= ay < -0.5f || IsGamepadButtonDown(p, GAMEPAD_BUTTON_LEFT_FACE_UP);
        in.down |= ay > 0.5f || IsGamepadButtonDown(p, GAMEPAD_BUTTON_LEFT_FACE_DOWN);
        in.jumpHeld |= IsGamepadButtonDown(p, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
        bool jp = IsGamepadButtonPressed(p, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
        bool ap = IsGamepadButtonPressed(p, GAMEPAD_BUTTON_RIGHT_FACE_LEFT);
        bool bp = IsGamepadButtonPressed(p, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT) || IsGamepadButtonPressed(p, GAMEPAD_BUTTON_RIGHT_TRIGGER_1);
        in.jumpPressed |= jp;
        in.attackPressed |= ap;
        in.abilityPressed |= bp;
        in.any |= jp || ap || bp;
    }
    return in;
}

bool InputMap::pausePressed() const
{
    if (pressed(pause_)) return true;
    for (int p = 0; p < 2; ++p)
        if (IsGamepadAvailable(p) && IsGamepadButtonPressed(p, GAMEPAD_BUTTON_MIDDLE_RIGHT)) return true;
    return false;
}

bool InputMap::menuUp() const
{
    if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) return true;
    for (int p = 0; p < 2; ++p) if (IsGamepadAvailable(p) && IsGamepadButtonPressed(p, GAMEPAD_BUTTON_LEFT_FACE_UP)) return true;
    return false;
}
bool InputMap::menuDown() const
{
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) return true;
    for (int p = 0; p < 2; ++p) if (IsGamepadAvailable(p) && IsGamepadButtonPressed(p, GAMEPAD_BUTTON_LEFT_FACE_DOWN)) return true;
    return false;
}
bool InputMap::menuSelect() const
{
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) return true;
    for (int p = 0; p < 2; ++p) if (IsGamepadAvailable(p) && IsGamepadButtonPressed(p, GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) return true;
    return false;
}
