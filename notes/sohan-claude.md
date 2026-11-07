# Status from Sohan's Claude (sohan-claude)

Updated: 2026-11-07

## 2026-11-07 (later): claude-sohan-story, a new story, "The Four Minutes"

Branch claude-sohan-story starts from claude/project-thread-70pj0g (the seven graphics stages).
Sohan asked for a brand new story with real character development.

### The story
Dementia takes the newest memories first, so the run travels backwards through a life while the
graphics travel forwards: the clearest memory (full 3D) is the oldest one.
- Player 1 is Maren Ashby, 81, a retired clockmaker in the seaside town of Saltmarch. Family words:
  "We keep the hour." Every clock she set ran four minutes slow.
- Player 2 is Wren, ten, in a blue cap. He is in every memory at every age and never grows up.
  The reveal: he is her little brother, who went through the millpond ice when she was fourteen.
  She was meant to walk him home at four and was four minutes late.
- The Forgetting (boss) is her own mind being merciful; it speaks a different plea in each stage.
- Stages: Larkfield House, The Shop, The Harbour, The Wedding, The Apprentice, The Long Winter, The Millpond.

### Done
- assets/strings.txt rewritten (all player-facing text), new keys story_0..6 and boss_says_0..6.
- Banners can carry a story line (Game::showBanner(..., story), drawn by Hud under the sub line):
  shown at the start of a run and at every stage upgrade. The boss banner shows boss_says_<stage>.
- The four ending pictures redrawn in tools/assetgen (endingPicture): Four o'clock at the millpond,
  the watch in the reeds at 4:04, the clock shop at 3:56, the millpond again in gold light.

### Files I touched that another Claude made
- src/core/Game.h/.cpp (showBanner signature, three banner calls), src/ui/Hud.cpp (banner drawing),
  tools/assetgen/SpriteGen.cpp (endingPicture only), assets/strings.txt, assets/art/ending_0..3.png.

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
