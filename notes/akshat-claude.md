# Akshat's Claude: status

## 2026-11-07: 8-bit art, seamless upgrades, two new bosses, new music
Branch claude-akshat, made from claude-sohan-upgrade at c349652 (Sohan's latest build).
Full plan and what's next: notes/akshat-plan.md.

### Done
- 8-bit players redrawn pixel by pixel (SpriteGen.cpp drawPlayer8), dusk 8-bit background,
  softer CRT lines, cleaner blob.
- Era upgrades no longer freeze the game: short glitch, no white flash, the sweep plays while
  you keep running (Game.cpp transition block, Renderer.h timings, Renderer::present).
- Boss kinds (Enemy.h BossKind): Glitch Golem in 8-bit (hops, landing shockwave), Eraser in
  16-bit (charges along a warned lane), Forgetting in 1-bit and 2.5D (2.5D mixes in charges).
  New sheets enemy_boss_golem.png / enemy_boss_eraser.png in every era.
- Music rebuilt (SoundGen.cpp): 16-bar build + drop loops, a boss track per era
  (music_boss.wav), sidechain pump. Audio crossfades to the boss track; the screen pulses on
  the beat (post.fs `beat`, Audio::beatPulse). Tempos live in src/audio/MusicSpec.h.
- New config keys: [boss] shockwave_reach, [audio] beat_pulse.

### Files I touched that others made (small changes, said here)
SpriteGen.cpp, SoundGen.cpp, Enemy.h/.cpp, Game.cpp, Renderer.h/.cpp, Art.h/.cpp,
Audio.h/.cpp, Hud.cpp, post.fs, strings.txt, config.ini, and the regenerated assets/art and
assets/audio files.

### Not done
- 16-bit and 2.5D player art unchanged.
- The autoplay bots still struggle with bosses; humans should be fine. Needs a real playtest.
- Built and run on macOS only.
