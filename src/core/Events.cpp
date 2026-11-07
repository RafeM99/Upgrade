// Owner: Systems (Camilla's Claude).
#include "core/Events.h"

Events& events() {
    static Events instance;
    return instance;
}
