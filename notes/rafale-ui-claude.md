# Status from Rafale's "UI changes" Claude

Thread: "Apply UI changes from notes". Branch: `claude/project-thread-e2fqcf`, started from main at dd87652.
Nothing pushed to main.

## 2026-11-08 early morning: every item in "ui changest.txt" worked through

raf asked to implement the review notes in `ui changest.txt` (repo root), using Blender for 3D models.

### 3D models (Blender, tools/blender/make_models.py, all .glb in assets/models)
- Grandfather clock in parts: case (plinth / long trunk / break-arch hood, swan neck, brass finials,
  columns, ivory dial with hour marks), pendulum, hour and minute hands, two bracket feet. The trunk
  is hollow; only the pendulum weight shows through a round window, so it never pokes through.
  The face (eyes, brows) is drawn in the arch above the dial.
- The Forgetting is now a ball of Gran's dark knitting wool: lumpy core, 20 yarn wraps, two
  crossed knitting needles, a hollow eye with a cyan ring and a separate pupil that follows the
  nearest player, two-ply twisted strand pieces (3 to 6 per strand) with frayed tips, paler
  towards the ends. Turned 0.3 rad from the camera.
- Gran: head 1.3x, bright blue glasses well in front of the face, no buttons or cheeks, perm
  curls instead of the flat fringe, one tapered cardigan shape instead of piled balls.
- Sam built in Blender on the same skeleton as Gran (one run cycle for both).

### Rendering
- lit3d.fs: `blendCol` and `grey` uniforms. Hit flashes blend to white, faded players go grey.
  See-through parts are drawn as an ordered dither, so no inside parts show.
- post.fs: FXAA for 2.5D/3D only; dead values and the stale header removed.
- 2.5D/3D fill any window shape (no bars on 16:10). Title/pause/ending pages centred on any shape.
- Retina: FLAG_WINDOW_HIGHDPI on Apple only, 3D target at framebuffer size. NOT tested on a Mac.
- Smoother sphere mesh, varied pebbles (0 to 3 per tile, sizes, tints, shapes), less even fence,
  lamp and garden spacing.

### 2D sprites (tools/assetgen/SpriteGen.cpp, 11 PNGs)
- 1-bit clock redrawn (no longer reads as a skull). Clocks in stages 4, 5, 6 are three different
  clocks. Boss sprites for stages 5 and 6 lean towards the wool ball. Blob changes shape per stage.

### Other
- Old 4-stage brief moved from the repo root to notes/archive/old-4-stage-brief.md (marked
  outdated); the empty "what just got created" removed.

### Files of other Claudes touched (on this branch only)
- src/render/Models3D.*, src/render/Renderer.*, src/core/Game.cpp (Apple HiDPI flag),
  src/ui/Hud.cpp, src/ui/Story.cpp (centring on non-16:9), assets/shaders/lit3d.fs, post.fs,
  tools/blender/make_models.py, tools/assetgen/SpriteGen.cpp, assets/models/*, 11 sprite PNGs.

### Still to do
- Play by a human, especially on raf's Mac (Retina path untested).
- 1-bit blob still floats 2 px in its frame.
