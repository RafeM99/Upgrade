#pragma once
// Owner: Systems (Camilla's Claude).
// Tiny event system. Areas talk through these signals, never by calling each other's code.
//   events().eraChanged.connect([](int era) { ... });
//   events().eraChanged.emit(2);
#include <functional>
#include <string>
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
    Signal<int> eraChanged;                             // new era: 0 = 1-bit, 1 = 8-bit, 2 = 16-bit, 3 = 2.5D
    Signal<int, int> fragmentCollected;                 // player index, amount
    Signal<int> playerFaded;                            // player index
    Signal<int> playerRevived;                          // player index
    Signal<int, const std::string&> abilityUnlocked;    // player index, ability name (e.g. "double_jump")
    Signal<> memoryLost;                                // both players Faded: bar drops
    Signal<> memoryComplete;                            // bar reached 100: ending
};

Events& events();
