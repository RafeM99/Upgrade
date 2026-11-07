// Events.h: a tiny event system so systems don't call into each other directly.
// Usage: events().eraChanged.listen([](int era){ ... });  events().eraChanged.emit(2);
#pragma once
#include <functional>
#include <vector>

template <class... Args>
class Event {
public:
    void listen(std::function<void(Args...)> fn) { fns_.push_back(std::move(fn)); }
    void emit(Args... args) const { for (auto& fn : fns_) fn(args...); }
    void clear() { fns_.clear(); }

private:
    std::vector<std::function<void(Args...)>> fns_;
};

struct Events {
    Event<int> eraChanged;                    // new era 0..4
    Event<int, float> fragmentCollected;      // player index, bar amount
    Event<int> playerFaded;                   // player index
    Event<int> playerRevived;                 // player index
    Event<int, const char*> abilityUnlocked;  // player index, ability id
    Event<> memoryLost;                       // both players Faded
    Event<> memoryComplete;                   // bar full in the last era and boss beaten
    Event<int, float, float> enemyKilled;     // enemy type, x, y
    Event<int, int> powerupCollected;         // player index, power-up type
    Event<> bossAppeared;
    Event<> bossDefeated;
    Event<int> playerJoined;
};

Events& events();
