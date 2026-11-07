#include "core/EraManager.h"
#include "core/Events.h"

void EraManager::setEra(int e) {
    if (e < 0) e = 0;
    if (e > kEraCount - 1) e = kEraCount - 1;
    era = e;
    events().eraChanged.emit(era);
}
