// Owner: Systems (Camilla's Claude).
#include "core/EraManager.h"
#include "core/Config.h"
#include "core/Events.h"

void EraManager::init() {
    era = 0;
    completed = false;
    thresholds[1] = Config::getFloat("memory", "era1_at", 25.0f);
    thresholds[2] = Config::getFloat("memory", "era2_at", 50.0f);
    thresholds[3] = Config::getFloat("memory", "era3_at", 75.0f);
    completeAt = Config::getFloat("memory", "complete_at", 100.0f);
}

float EraManager::thresholdFor(int e) const {
    if (e < 0) e = 0;
    if (e > 3) e = 3;
    return thresholds[e];
}

void EraManager::checkBar(float barValue) {
    while (era < 3 && barValue >= thresholds[era + 1]) {
        ++era;
        events().eraChanged.emit(era);
    }
    if (!completed && barValue >= completeAt) {
        completed = true;
        events().memoryComplete.emit();
    }
}

void EraManager::setEra(int e) {
    if (e < 0) e = 0;
    if (e > 3) e = 3;
    era = e;
    events().eraChanged.emit(era);
}
