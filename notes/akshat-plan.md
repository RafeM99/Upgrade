# Upgrade: the plan forward (Akshat)

Updated: 2026-11-07. Branch: claude-akshat (built on Sohan's claude-sohan-upgrade).

## What the game is
Two players (Gran and her grandson Sam) run right through a memory that is fading. The
Forgetting Wall chases them from the left. Stomping creatures drops fragments that fill the
Memory Bar. A full bar brings a boss; beating it "upgrades" the world to the next era of
graphics: 1-bit, 8-bit, 16-bit, then 2.5D. Each era unlocks new abilities. Beating the last boss
restores the memory and plays the ending.

The theme is time: the same song, the same two people, the same park, remembered on better
and better hardware.

## Done on this branch (2026-11-07)
- 8-bit art: players are hand-placed pixel art on the 16 x 24 grid (big heads, solid colour
  blocks, two-pixel limbs), instead of shapes shrunk down until they blur. Dusk background
  (dark navy sky, NES-style stripe gradient, silhouette trees and fences) so the bright
  characters, enemies and ground stand out. Softer CRT scanlines. Cleaner blob enemy.
- Seamless era upgrades: the game no longer freezes for 3-4 seconds. You keep running while the
  world sweeps into the next era. Shorter, lighter glitch and no white flash. Players can't be
  hurt during it, and the Wall eases back.
- Bosses: one per era.
  - 1-bit: The Forgetting (swoops down, rests, stomp it).
  - 8-bit: Glitch Golem (new). Hops at you; each landing sends a shockwave along the ground,
    so you jump to dodge. After every third hop it is dizzy: stomp it.
  - 16-bit: The Eraser (new). Lines up with a player (a red lane with arrows warns you),
    charges across the screen, crashes and is dizzy: stomp it.
  - 2.5D: The Forgetting comes back whole: it mixes swoops and charges.
- Music, Geometry Dash style: faster tempos (110 / 140 / 150 / 128 BPM), a 16-bar loop with
  an 8-bar build (snare roll and riser) into an 8-bar drop (four-on-the-floor kick, off-beat
  bass, 16th arpeggios, sidechain pumping). Each era has its own boss track that crossfades in
  when the boss appears. The screen pulses on every beat.

## Next, in order
1. Playtest pass (everyone). Is the 8-bit era readable now? Are Golem shockwaves fair
   (config.ini [boss] shockwave_reach)? Is the beat pulse too much (config.ini
   [audio] beat_pulse, 0 turns it off)?
2. Bring the same hand-pixelled treatment to 16-bit players (32 x 48). It is the next weakest
   era. Then hand-pixel the 8-bit enemies the same way.
3. Rhythm in the levels, the real Geometry Dash part: put jump pads, spikes and enemy
   spawns on the beat. The game already knows the beat (Audio::beatPulse / beatIndex). Ideas:
   blocks that blink in and out with the bass, flyers that dive on the downbeat, a "perfect
   jump" bonus when you jump on a beat.
4. Boss polish: each boss gets a second phase at half health (Golem hops twice as fast,
   Eraser charges twice in a row), and a short intro where its name slams on screen.
5. Audio polish: separate sounds for each boss (Golem rumble, Eraser squeak), a level-up
   stinger timed so the drop lands as the new era appears.
6. Era transitions: carry the beat through them. Start the upgrade sweep on a bar line, so the
   new era's music drops exactly as the sweep finishes.
7. Menus and feel: settings screen (music/sfx volume, screen shake, beat pulse), a
   practice mode that starts on any era, and a best-score table.
8. Ship: Windows and Linux builds checked, itch.io page, trailer capture with `--autoplay`.

## Files I own or changed
New: src/audio/MusicSpec.h, notes/akshat-plan.md, notes/akshat-claude.md.
Changed (small, said here): tools/assetgen/SpriteGen.cpp (8-bit players and background, two
new boss sheets), tools/assetgen/SoundGen.cpp (music), src/enemies/Enemy.h/.cpp (boss kinds),
src/core/Game.cpp (transition, shockwave, boss music), src/render/Renderer.h/.cpp and
Art.h/.cpp (boss sheets, charge lane, beat pulse), src/audio/Audio.h/.cpp (boss track, beat
clock), src/ui/Hud.cpp (boss names), assets/shaders/post.fs, assets/strings.txt, and all
generated art and audio under assets/.
