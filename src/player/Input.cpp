#include "player/Input.h"
#include "core/Config.h"
#include "core/Paths.h"
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
    firstPadPlayer_ = config().i("input.first_pad_player", 1) == 2 ? 2 : 1;
    // Extra controller mappings (SDL_GameControllerDB format), if the team adds the file.
    std::string db = assetPath("gamecontrollerdb.txt");
    if (FileExists(db.c_str())) {
        char* text = LoadFileText(db.c_str());
        if (text) { SetGamepadMappings(text); UnloadFileText(text); }
    }
}

void InputMap::beginFrame()
{
    for (int i = 0; i < 4; ++i) {
        prevStickY_[i] = stickY_[i];
        stickY_[i] = IsGamepadAvailable(i) ? GetGamepadAxisMovement(i, GAMEPAD_AXIS_LEFT_Y) : 0.0f;
    }
}

int InputMap::padCount() const
{
    int n = 0;
    for (int i = 0; i < 4; ++i) if (IsGamepadAvailable(i)) ++n;
    return n;
}

// Pads are handed out in slot order, whatever slots they sit in: the first one found plays
// Gran (or Sam, with first_pad_player = 2), the next one the other player.
int InputMap::padFor(int player) const
{
    int found[4], n = 0;
    for (int i = 0; i < 4; ++i) if (IsGamepadAvailable(i)) found[n++] = i;
    int order = player;
    if (firstPadPlayer_ == 2) order = n == 1 ? (player == 1 ? 0 : -1) : (player == 0 ? 1 : 0);
    return order >= 0 && order < n ? found[order] : -1;
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
    const int pad = padFor(p);
    if (pad >= 0) {
        float ax = GetGamepadAxisMovement(pad, GAMEPAD_AXIS_LEFT_X), ay = GetGamepadAxisMovement(pad, GAMEPAD_AXIS_LEFT_Y);
        in.left |= ax < -0.35f || IsGamepadButtonDown(pad, GAMEPAD_BUTTON_LEFT_FACE_LEFT);
        in.right |= ax > 0.35f || IsGamepadButtonDown(pad, GAMEPAD_BUTTON_LEFT_FACE_RIGHT);
        in.up |= ay < -0.5f || IsGamepadButtonDown(pad, GAMEPAD_BUTTON_LEFT_FACE_UP);
        in.down |= ay > 0.5f || IsGamepadButtonDown(pad, GAMEPAD_BUTTON_LEFT_FACE_DOWN);
        in.jumpHeld |= IsGamepadButtonDown(pad, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
        bool jp = IsGamepadButtonPressed(pad, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);                     // A
        bool ap = IsGamepadButtonPressed(pad, GAMEPAD_BUTTON_RIGHT_FACE_LEFT) ||                   // X
                  IsGamepadButtonPressed(pad, GAMEPAD_BUTTON_LEFT_TRIGGER_1);                      // LB
        bool bp = IsGamepadButtonPressed(pad, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT) ||                  // B
                  IsGamepadButtonPressed(pad, GAMEPAD_BUTTON_RIGHT_TRIGGER_1);                     // RB
        bool yp = IsGamepadButtonPressed(pad, GAMEPAD_BUTTON_RIGHT_FACE_UP);                       // Y
        if (yp) { bp = true; in.down = true; }   // Y is the second ability: the same as down + B
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
    for (int p = 0; p < 4; ++p)
        if (IsGamepadAvailable(p) && IsGamepadButtonPressed(p, GAMEPAD_BUTTON_MIDDLE_RIGHT)) return true;
    return false;
}

bool InputMap::menuUp() const
{
    if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) return true;
    for (int p = 0; p < 4; ++p) {
        if (!IsGamepadAvailable(p)) continue;
        if (IsGamepadButtonPressed(p, GAMEPAD_BUTTON_LEFT_FACE_UP)) return true;
        if (stickY_[p] < -0.6f && prevStickY_[p] >= -0.6f) return true;   // stick pushed up
    }
    return false;
}
bool InputMap::menuDown() const
{
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) return true;
    for (int p = 0; p < 4; ++p) {
        if (!IsGamepadAvailable(p)) continue;
        if (IsGamepadButtonPressed(p, GAMEPAD_BUTTON_LEFT_FACE_DOWN)) return true;
        if (stickY_[p] > 0.6f && prevStickY_[p] <= 0.6f) return true;     // stick pushed down
    }
    return false;
}
bool InputMap::menuSelect() const
{
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) return true;
    for (int p = 0; p < 4; ++p) if (IsGamepadAvailable(p) && IsGamepadButtonPressed(p, GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) return true;
    return false;
}

bool InputMap::menuBack() const
{
    for (int p = 0; p < 4; ++p) if (IsGamepadAvailable(p) && IsGamepadButtonPressed(p, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT)) return true;
    return false;
}
