# Status from Rafale's "graphics stages" Claude

Thread: "Add graphics evolution stages". Branch: `claude/project-thread-70pj0g`, built on Sohan's
`claude-sohan-upgrade` (c349652), which builds on Rafale's cloud Claude's game. Nothing pushed to main.

## 2026-11-07 (late night): Blender models for stage 6 (Gran and the bat boss)

- raf asked for Blender-made 3D models for the last stage only. tools/blender/make_models.py builds
  Gran (coat, scarf, glasses, bun, jointed arms and legs) and a bat boss (furry body, tall ears,
  two-part wings) and exports one .glb per moving part into assets/models/.
  Later the same night the bat was replaced by the Forgetting itself: a big floating tentacle
  alien (one huge eye, eye stalks, glowing antennae, eight waving tentacles)..
- Rebuild them: pip install bpy (Blender as a Python module, ~400 MB), then
  python tools/blender/make_models.py assets/models. The .glb files are committed, so playing
  needs nothing extra.
- src/render/Models3D.*: loads the parts, run cycle for Gran, wing beats for the bat; falls back
  to the old primitive models if the files are missing. Stage 6 only.

## 2026-11-07 (late night): backgrounds repainted from raf's reference pictures

- Merged claude-camilla (boss powers, difficulty) after PR #5 went to main.
- Each stage's background painter now lives in tools/assetgen/bg/eraN.inc (included into
  SpriteGen.cpp before bgLayer). New scenes in the cluster-tree / treehouse style raf sent:
  0 winter night street, 1 garden and washing line, 2 school street with chunky trees,
  3 treehouse woodland (camping trip), 4 railway town and viaduct, 5 hay meadow, 6 morning hills.

## 2026-11-07 (night): story merge undone at raf's request

raf asked to go back to before the Maren storyline. I reverted (no force push, no history
rewrite) my merge of main's "The Four Minutes" commit and my sprite changes for it. The game
code on my branch is again exactly as at 1884f73. When PR #5 merges, main loses the story too;
Sohan's commit stays in history and can be brought back with git revert. I kept
notes/sohan-claude.md as Sohan wrote it.

## 2026-11-07 (evening): engineer's review, worked through with helpers

raf forwarded a graphics engineer's review (stages looked AI-made). Done, in its fix order:
- Pixel stages render at native size (88/176/176/220/352 px tall), ortho camera snapped to the
  pixel grid, whole-number scaling, and post.fs reads each native pixel at its centre (an old
  half-texel shift made 1-bit edges ragged). No gap at the left edge.
- Hue-shifted flat shading, one top-left light, exact 4 greys on stage 1, hard-edged blit.
- Hand-placed dirt, grass with a light top line and dark lip, platform material per stage,
  undersides on floating tiles. Hand-laid backgrounds with props, faded toward the sky.
- New sprites: 1-bit bitmaps, Gran ages, Sam grows up, enemies and fragments per stage.
- HUD per stage (src/ui/Hud.cpp mostly rewritten), whole-number text, hint shown once as a
  dialogue from Gran.
- 3D: sky visible, outlines, varied trees, Gran's cottage landmark. 2.5D: one projection,
  ground fills the screen. New shader assets/shaders/haze.fs.

## 2026-11-07 (later): flat, crisp restyle after raf's feedback

raf said the first pass looked glossy and did not follow the reference picture. Changed:
- post.fs: no bloom, glow, vignette, scanlines, LCD grid or colour grading on any stage; only each
  stage's colour limit (2 colours, 4 greys, 16 colours, 15-bit colour).
- SpriteGen: hard-banded cel shading (no gradients, no specular, no rim light), ordered-dither
  "static" instead of random noise, hard alpha and outlines on every stage, banded skies and hills.
  All art regenerated, including the 2.5D set and the ending pictures.
- 2.5D/3D textures use point filtering. 3D models: low-poly meshes and two-band flat lighting,
  no fog or highlights. Tile lighting has no coloured glows. HUD bars have no gloss.

## 2026-11-07: seven graphics stages, a level set per stage, faster Memory Bar

raf asked for a stage for each step of handheld graphics history (a reference picture of the
fidelity steps; none of its characters, art or palettes are used) with levels for each, and a
Memory Bar that fills faster.

### Done
- The game now has 7 stages instead of 4 eras (the code still calls them `era`, 0..6):
  0 1-bit, 1 4-shade (new), 2 8-bit, 3 16-bit, 4 32-bit late 2D (new), 5 2.5D, 6 full 3D (new).
  All per-stage tables are in `src/render/ArtSpec.h` (tile size, drawing style, camera swing,
  sound set, difficulty tier).
- Art: `tools/assetgen/SpriteGen.cpp` draws the two new pixel sets (4-shade: four warm greys with a
  checker on mid tones; 32-bit: 48 px tiles, lit 4-tone ramps with cool shadows, a rim light and
  coloured outlines), new backgrounds (Gran's garden; the town and the railway viaduct; a morning
  sky for 3D). Art folders were renamed to match: era1 -> era2, era2 -> era3, era3 -> era5. The old
  sets regenerate byte-identical.
- Full 3D stage: `src/render/Models3D.*` + `assets/shaders/lit3d.*`. Gran, Sam, every enemy, the boss
  and the fragments are modelled from lit primitives and animated; the street (lawn, fence, trees,
  houses, lamps, flowers) is built around the path. The camera swings on from 2.5D to a 3D view.
- Post shader looks for the new stages in `assets/shaders/post.fs` (dot-matrix LCD for 4-shade,
  backlit glow for 32-bit, filmic tone mapping for 3D). HUD bar styled per stage.
- Levels: chunk headers take `stages=1,2` (made for those stages, preferred there by
  `level.stage_chunk_share`) and `min_stage=N`. `min_era` still works (mapped to the new stages).
  21 new chunks `s0_*` .. `s6_*`, plus intro gates for every stage (`intro_stage1..6`;
  intro_era1/2/3 were renamed to intro_stage2/3/5).
- Abilities spread over the stages: 1 double jump + dash, 2 attack button, 3 ground pound,
  4 wall jump, 5 glide, 6 time-slow. Wall, enemy and boss difficulty use the old four-step scale.
- Memory Bar: `memory.fill_speed = 2.0` in config.ini multiplies every way the bar fills.

### Files I changed that other Claudes made (small, said here)
- src/core/Game.*, src/render/Renderer.*, Art.cpp, ArtSpec.h, GameCamera.cpp, src/ui/Hud.cpp,
  src/audio/Audio.cpp (stage -> sound set), src/level/Chunk.*, Level.*, src/player/Abilities.h,
  tools/assetgen/SpriteGen.cpp, assets/shaders/post.fs, assets/config.ini, assets/strings.txt
  (stage names, memory names), PLAY.md (stage list and options). Intro chunk files renamed.

### Not done / for the others
- Sound still has 4 sets; the new stages share the nearest one (`AUDIO_SET` in ArtSpec.h).
- Ending pictures are unchanged (4 memories).
