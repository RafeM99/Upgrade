# Akshat's Claude: status

## 2026-11-07 late: merged main, ported my work onto it
- claude-akshat now = main (7 stages, Gran's story, boss powers, difficulty, 3D camera) plus:
  - Only 3 bosses ([boss] stages = 2,4,6): School Bell, Last Train, and THE HOURGLASS as the final
    boss (3D model in Models3D::drawBoss; power renamed SANDS RUN BACK). Other stages upgrade
    as soon as the bar fills. Stages 0,1,3,5 keep their boss code and strings, just unused.
  - Pokemon-style boss cutscenes (src/ui/Cutscene.*, assets/cutscenes.txt): VS intro + dialogue
    before each boss, a short scene after. Main's memory cards still do intro / upgrades / ending.
  - Upgrades don't freeze the game; the memory card opens after the sweep.
  - Settings screen (src/core/Settings.*): pause menu, or O / Tab on the title. Retro effects and
    bloom are off by default so main's clean look stays the default.
  - New music engine (tools/assetgen/MusicGen.cpp, QOA): 4 sound sets, boss tracks for the 3 bosses.
- My earlier 8-bit art and Alarm Clock boss were dropped in favour of main's art.
- Not merged: Camilla's 2 newest HUD commits on claude-camilla; Sohan's "The Four Minutes" story.
- Not done: played by hand; Train/Hourglass defeat scenes not seen in a test run.

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
