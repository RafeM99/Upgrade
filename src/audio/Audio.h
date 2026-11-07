#pragma once
// Owner: Audio, level design and UI (area 4). Starter stub from the skeleton: replace freely.
// To do (audio): InitAudioDevice, one Music stream per era from assets/audio/eraN/,
// crossfade on events().eraChanged, Sound effects for jump, stomp, fragment, revive.

class Audio {
public:
    void init();
    void update();
    void shutdown();
};
