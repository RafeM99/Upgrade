# Status from Sohan's Claude (sohan-claude)

Updated: 2026-11-07

## 2026-11-07: console-eras rage platformer on claude-sohan-mario

### Done
- Branch claude-sohan-mario, cut from the claude-camilla skeleton at 5d74bd6 (before the skeleton was removed upstream).
- Reworked the skeleton into a Mario-style rage platformer:
  - Run (hold attack), variable jump, stomp enemies, bonk blocks from below.
  - Coin blocks, bricks that break, pipes, coins, a flagpole at the end of each world.
  - Rage traps: hidden blocks over pits, fake floors that crumble, troll "?" blocks that spit enemies,
    hidden spikes, coins that turn out to be enemies, flagpoles that run away, a death counter with taunts.
  - Eras by world: 1 NES, 2 SNES, 3 N64, 4 GameCube, 5 Wii. Each has its own render resolution,
    filter, camera, palette, background and HUD style.
  - Endless: after world 5 the worlds never stop. Each is one chunk longer (up to 10),
    enemies get faster, flags run further, and the later-era trap chunks show up more.
  - Co-op: P2 joins with any button; a player left off-screen for 1.5 s is pulled to their partner.
    Colour-block co-op chunks only appear when P2 has joined.
- Removed MemoryBar and ForgettingWall (not used by this game).
- Tested headless with --autoplay (with and without --god, and with --fill): worlds 1 to 9 cleared, no crashes.

### Files on this branch
Everything under src/ and assets/ differs from the skeleton. This branch is a separate game, not meant to merge into claude-camilla.

### Not done
- No audio yet (src/audio is still a stub).
- Not pushed. Not tested on Windows or Linux.
