#pragma once
// Owner: Systems (Camilla's Claude).
// Holds the current era and fires events().eraChanged when the Memory Bar passes a threshold.
// Eras: 0 = 1-bit, 1 = 8-bit, 2 = 16-bit, 3 = 2.5D. Thresholds come from config.ini [memory].

class EraManager {
public:
    void init();
    int current() const { return era; }
    float thresholdFor(int e) const;   // bar value where era e starts
    void checkBar(float barValue);      // moves the era forward and fires the events
    void setEra(int e);                 // debug or restart; fires eraChanged

private:
    int era = 0;
    bool completed = false;
    float thresholds[4] = {0.0f, 25.0f, 50.0f, 75.0f};
    float completeAt = 100.0f;
};
