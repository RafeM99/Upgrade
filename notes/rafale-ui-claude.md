# Status from Rafale's "UI changes" Claude

## 2026-11-08 (evening): ground upgrade round 2 (baked chunks, CC0 textures, field, flowers)

- Baked static meshes (Models3D bakeBegin/recordPart/drawChunk): part() calls inside a bake are merged per texture into one
  mesh per chunk of columns (ground 16 wide, field 24 wide, flowers 16 wide), built once from world hashes, rebuilt only when
  the chunk's tiles change, evicted far from the camera. A frame costs a few DrawMesh calls instead of thousands. Frame time in
  the software renderer is about +6% on era 5 (400 frames 17.8 s -> 19.0 s) with much more geometry; draw calls were not the
  only cost there (fill and triangles are).
- Ground: grass cap now has standing blades (3 shapes, scale 0.6-1.2, tilt, noise-ramp greens), CC0 Poly Haven textures
  (grass/earth/rock, see assets/textures/SOURCES.md) multiplied into the vertex colours via world-space planar UVs; pebbles use
  a low-poly sphere. Only the pulsing memory blocks ('1'/'2') are still drawn live.
- Mid-field: the flat lawn bands are gone. drawField builds a displaced height field (flat near the path so fence/houses/lamps
  stay put, rolling further back), jittered-Voronoi patchwork of fields (hue +-0.04, per-field brightness; ripe corn; crop rows
  drawn in lit3d.fs from a vertex alpha below 1 and world z, faded where they would alias), hedgerows and far trees on field
  edges (none near the path or the cottage).
- Flowers: three species (poppy/buttercup, daisy/pink, cornflower/violet), five squashed-sphere petals, 5-15 degree bent stalk in
  two pieces, two leaves, scale 0.7-1.3, hue jitter +-0.03, noise-masked clumps; baked per chunk the first time a clump is asked for.
- NOT done: Blender make_ground.py kit (no Blender or bpy in this container; the baked code meshes replace it), and the 2D era
  tile repaint (era0-4 tiles.png already have 8 grass/earth variants, deep stratum, rocky undersides, tufts and edge caps; left alone).
- Files touched: Models3D.cpp/.h, assets/shaders/lit3d.fs, assets/textures/ (3 new CC0 + 3 derived), SOURCES.md.

## 2026-11-08 (later): era 6 ground and backdrop "read as generated" pass

- Contact shadows: soft dark decals (Models3D::shadowAt/flushShadows) under houses, cottage, trees, lamps, fence posts,
  stepping stones, flagstones, stooks and bales. Collected in a dry first pass, laid on the lawn before props are drawn.
- Ground: no Blender tile kit (too many draws for the software renderer); instead per-tile boxes with variation:
  grass lip with different overhang/height per tile, yellow-green lit top and blue-green shaded lip face, hanging blades,
  soil darkened under the lip, wavy earth strata, non-overlapping pebbles sunk a third (more deeper down), tapering roots.
  lit3d.fs has a new `macro` uniform: large world-space noise over lawns/ground (light/dark patches, hue shift).
- Front lawn: bevelled stepping stones with colour variation, pond with mud ring, shallow/deep water and reeds, flowers with
  bent stalks, leaves and petals (close ones), hue jitter. Trees: 5-7 blobs, dark bottom to light top, flared trunk.
- Backdrop: tools/backdrop/make_era6_backdrop.py renders assets/art/era6/bg_0.png (smooth dithered sky, sun glow, soft clouds)
  and bg_1.png (ridged-multifractal peaks with snow/rock, forest foothills, farmland with crop rows, hedgerows, varied trees),
  anti-aliased, 2560x720. haze.fs now has a vertical haze gradient and soft alpha. Backdrop layer moved to z -88 (it was cut
  by the far plane on the right).
- Not done: Blender make_ground.py kit, cloud sprites as separate geometry, crop-row displaced mid-field geometry (the
  flat lawn bands are only broken up by the macro noise). No textures downloaded: everything is procedural.
- Files touched: Models3D.cpp/.h, Renderer.cpp (day layer), lit3d.fs, haze.fs, era6 bg_0/bg_1.png, tools/backdrop/.

## 2026-11-08 morning: CC0 textures from Poly Haven

- assets/textures/ holds natural_walnut_veneer and knitted_fleece (CC0, 1k diffuse maps), with SOURCES.md.
- make_models.py (FILE_TEXTURES) packs the walnut into the clock's wood and a grey, brightened copy of the fleece
  (knitted_fleece_wool_512.jpg) into the Forgetting's wool. The glTF exporter drops a Blender BW/Math chain, so
  anything else than image times a colour has to be prepared as a file first (the raw fleece went near black).
- Needs polyhaven.com, api.polyhaven.com and dl.polyhaven.org allowed in the cloud environment to re-download.
- Gran/Sam critique rework (metal, baked AO, new faces, wool yarn) is in 6ba4d1c.

Thread: "Apply UI changes from notes". Branch: `claude/project-thread-e2fqcf`, started from main at dd87652.
Nothing pushed to main.

## 2026-11-08: model pipeline and asset critique (Blender parts, lit3d shader)

- tools/blender/make_models.py: real metallic/roughness, smooth by angle 30, bevels with hardened
  normals, merge by distance, Smart UV, Cycles AO baked into COLOR_0 alpha, procedural walnut
  grain and ivory mottle baked to textures packed in the .glb (texture downloads were approved
  later but the proxy blocks polyhaven/ambientCG, so all textures are procedural; no assets/textures).
- Gran/Sam faces (eyes with catchlight, Bezier smile, cheeks, blended nose), Gran perm shell and
  glasses on the nose, Sam hair clumps, bent hood, trainers, light-wash jeans. Clock 2.0 tall with
  brass, walnut, ivory, bracket feet. Forgetting: one wound yarn strand, pale eye, glowing ring.
- assets/shaders/lit3d.vs/.fs: vertex colour (rgb tint, alpha AO), texture0 via useTexture,
  stepped toon highlight on metals. Models3D::modelPart draws mesh by mesh (metallic, emissive,
  useTexture per material); clock pivot constants updated. Scenery code untouched.

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
