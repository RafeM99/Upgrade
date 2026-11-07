# Status from Sohan's Claude (sohan-claude)

Updated: 2026-11-07

## 2026-11-07: claude-sohan-upgrade (built on Rafale's cloud Claude's full game)

Branch claude-sohan-upgrade starts from claude/project-thread-y8u0w2 at 36a0307. Sohan asked for better
levels, real transitions between eras, and a stronger pixel-art retro-to-modern look.

### Done
- Era upgrade transition: the old era shudders, tears and goes blocky; at the switch a bright scanline
  sweeps left to right, and behind it the world is redrawn in the new era, sharpening as it goes.
  Both eras are rendered during the sweep (Renderer::drawEra / blitEra / drawSweep).
- Pixel scaling: post.fs now does "sharp" sampling, so every source pixel is an even block at any window size.
- Retro screens: 1-bit gets a handheld LCD pixel grid, 8-bit gets CRT scanlines between pixel rows.
- 1-bit art cleaned up in tools/assetgen: ordered halftone hills and a brick pattern instead of random
  dots, background drawn at full strength. Only assets/art/era0 PNGs were regenerated.
- 14 new chunks: 3 per era built around that era's abilities, plus an intro gate per era
  (intro_era1/2/3) whose fragments spell 8, 16 and 3D. The gate is placed straight after each upgrade:
  the unseen level past the screen is rebuilt (Level::truncateFrom, ChunkSpawner::forceChunk).
- Generated chunks down from 30% to 15%; lookahead set in config.ini [level] lookahead_tiles.

### Files I touched that another Claude made (small, said here)
- src/render/Renderer.h/.cpp, assets/shaders/post.fs, src/core/Game.cpp (switchEra, startRun, flash),
  src/level/Level.h/.cpp, tools/assetgen/SpriteGen.cpp (era 0 only), assets/config.ini [level].
- PLAY.md is unchanged.

### Not done
- The autoplay bots still stall on c05_tower and c06_double; humans can pass them.
- Not built on Windows or Linux.
