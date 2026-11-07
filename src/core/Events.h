#pragma once
// Tiny event system. Areas talk through these signals, never by calling each other's code.
//   events().eraChanged.connect([](int era) { ... });
//   events().eraChanged.emit(2);
#include <functional>
#include <vector>

template <typename... Args>
struct Signal {
    std::vector<std::function<void(Args...)>> listeners;
    void connect(std::function<void(Args...)> fn) { listeners.push_back(std::move(fn)); }
    void emit(Args... args) {
        for (size_t i = 0; i < listeners.size(); ++i) listeners[i](args...);
    }
};

struct Events {
    Signal<int> eraChanged;      // new era: 0 NES, 1 SNES, 2 N64, 3 GameCube, 4 Wii
    Signal<int> playerDied;      // player index
    Signal<int> playerJumped;    // player index
    Signal<> coinCollected;
    Signal<> enemyStomped;
    Signal<> worldCleared;       // a flag was reached
    Signal<> gameComplete;       // the Wii era's flag was reached
};

Events& events();
