#pragma once
// Owner: Core player (area 2). Starter code from the skeleton: replace freely.
// Keyboard and gamepad input for both players. Bindings are in the table in Input.cpp.

struct InputState {
    float moveX = 0.0f;           // -1 left .. +1 right
    float moveY = 0.0f;           // -1 down .. +1 up
    bool jumpHeld = false;
    bool jumpPressed = false;     // presses are kept until the next fixed update reads them
    bool attackPressed = false;
    bool abilityPressed = false;
    bool anyPressed = false;      // any of this player's buttons: used for P2 joining
};

class Input {
public:
    bool autoplay = false;        // test mode: both players run right and jump

    void poll();                  // once per rendered frame
    InputState take(int player);  // once per fixed update; clears the presses

private:
    InputState state[2];
    long frame = 0;
};
