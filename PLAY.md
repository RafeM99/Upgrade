# Upgrade: build, play and test

Everything here is on branch `claude/project-thread-y8u0w2` (Rafale's cloud Claude).

## Build and run
You need CMake 3.16+ and a C++17 compiler. raylib 5.5 is downloaded by CMake.

```
cmake -S . -B build
cmake --build build --config Release
./build/upgrade            # Windows: build\Release\upgrade.exe
```
The build copies `assets/` next to the executable, so the build folder can be zipped and run elsewhere.
Linux needs the X11/GL dev packages raylib lists (libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev).

## Controls
| | Player 1 (Gran, orange) | Player 2 (Sam, blue) | Gamepad |
|---|---|---|---|
| Move | A / D | Left / Right | stick or d-pad |
| Jump (hold to glide in 2.5D) | Space | Right Ctrl or Enter | A |
| Attack (from 8-bit on) | F | Right Shift | X or LB |
| Ability | G | / | B or RB |
| Second ability | S + G | Down + / | Y (or down + B) |
| Pause | Esc or P | | Start (B resumes) |

Player 2 joins at any time by pressing one of their buttons. Keys are set in `assets/config.ini` under `[input]`.

Gamepads: plug in at any time, in any USB slot. The first pad found plays Gran and the second plays Sam; each
player keeps their keyboard keys too, and a notice says who each pad plays as. To give a single pad to Sam
(Gran on the keyboard), set `first_pad_player = 2` under `[input]`. Menus, the difficulty selector, story cards
and the ending all work with the d-pad or left stick, A to pick and B to go back. Xbox, PlayStation and Switch
Pro pads are recognised, and `assets/gamecontrollerdb.txt` (SDL_GameControllerDB, zlib licence in
`assets/gamecontrollerdb_LICENSE.txt`) adds about 2,000 more. The title screen says which controllers the game
can see and who each one plays as. Rumble is not available: raylib does not support it on desktop.

8BitDo Ultimate 2C (wired) on a Mac: out of the box it starts in XInput mode, which macOS does not see as a
controller. Unplug it, hold B, plug it back in, then let go of B: it starts in DirectInput mode, which macOS
and the game recognise. If the title screen still says "No controller found", check that the Mac sees it at
all: Apple menu > About This Mac > More Info > System Report > USB should list an 8BitDo device.

Abilities: P1 gets double jump (8-bit), ground pound (16-bit, Ability in the air) and glide (2.5D, hold jump).
P2 gets dash (8-bit, Ability), wall jump (16-bit, jump against a wall) and the time-slow pulse (2.5D, Down + Ability).
Playing alone, Player 1 gets both sets so every chunk can be passed.

## The story
Gran (Edie, 80) has Alzheimer's. It takes the newest memories first and keeps the oldest longest.
Her grandson Sam walks back through her life with her, one memory per stage, newest first:
last winter (1-bit), 2014, 2006, 1979, 1964, 1958, and her earliest memory, 1951 (full 3D).
Every memory she gets back, she sees more clearly: that is the graphics upgrade.

The story has its own UI (`src/ui/Story.*`), separate from the level HUD:
- a memory card before each stage: a photograph of that memory taken from the game, its year,
  a few lines from Gran and Sam, and a timeline that moves further back each time;
- black cinematic bars when a boss arrives and when a memory comes back;
- the ending: an album of the seven photographs, then the present day.

Each memory has its own boss, the Forgetting in the shape of something from it:
| Stage | Memory | Boss | How it fights | Its power over time |
|---|---|---|---|---|
| 1-bit | The night she got lost | The 3 a.m. Clock | ticks from place to place | TIME STOP: freezes you for a moment |
| 4-shade | Arthur's shirts | The Empty Shirt | flutters wide, glides in on the wind | REWIND: takes back a hit unless you hit it again quickly |
| 8-bit | Sam's first day at school | The School Bell | rings when it lands and throws you back | HURRY: the whole world runs fast |
| 16-bit | The camping trip | The Storm | aims longer, strikes like lightning, rains flyers | HEAVY RAIN: time drags, you move slowly |
| 32-bit | The train to the city | The Last Train | drops at one side, charges along flat ground | ECHOES: copies of each charge follow it down the line |
| 2.5D | The hay meadow | The Scarecrow | calls crows | HARVEST RUSH: more crows, flying fast |
| 3D | Home | The Forgetting | as before | PULL BACK: drags you back to where you stood seconds ago |

Powers are announced first: the boss stops and pulses, and the line under its health bar names what is coming.

### Difficulty
Choose on the title screen with Up / Down (or 1, 2, 3), or start with `--difficulty gentle|medium|hard`.
The default is `[boss] difficulty` in `assets/config.ini`. All the numbers are in `POWER_TUNING` in `src/enemies/BossPowers.h`.
- Gentle: powers are short and clearly announced; a frozen player cannot be hurt.
- Medium: powers last longer and can hurt you (the clock attacks while you are frozen, echoes hurt).
- Hard: stronger and more frequent powers, and every boss takes one more hit.

All the words are in `assets/strings.txt`. `--no-story` (or `[story] enabled = 0`) turns the story UI off.

## How a run goes
1. Run right. Jump on the creatures of the Forgetting. They drop memory fragments that fill the Memory Bar.
2. The Forgetting Wall comes from the left. Touch it and you are Faded: touch your partner to come back.
   Both Faded = the memory slips (bar -10) and you respawn together.
3. When the bar is full, The Forgetting (mini-boss) appears. It marks where it will land, swoops,
   then rests on the ground: jump on it. Beat it and the world upgrades to the next graphics stage:
   1-bit, 4-shade, 8-bit, 16-bit, 32-bit, 2.5D (the camera swings), then full 3D.
4. Beat it in the 3D stage and the memory is restored: the ending and credits.

Each stage has its own sprites, tiles, palette, screen look and level chunks (`stages=` in a chunk header).
New abilities by stage: 4-shade double jump (P1) and dash (P2), 8-bit attack button, 16-bit ground pound,
32-bit wall jump, 2.5D glide, 3D time-slow pulse.

Power-ups: Keepsake locket (one free hit), Hourglass (wall falls back and slows), Lullaby (fragment magnet),
Old photograph (+8 bar), Old trainers (run faster).

## Change things without code
- `assets/config.ini`: every number (speeds, jump height, fragment value, wall speed, boss hits, power-ups, keys).
- `assets/strings.txt`: every line of text, the memory names and the ending.
- `assets/chunks/*.txt`: level chunks. Format and legend are at the top of `src/level/Chunk.h`.

## Art and sound
All sprites and sounds are generated by code in `tools/assetgen`. The PNG and WAV files are committed.
To change them, edit the drawing code and run:
```
cmake --build build --target assetgen
./build/assetgen assets
```

## Test options
`--autoplay` (bots play), `--two` (P2 joined), `--fill` (bar fills fast), `--era N` (start in stage 0-6:
0 1-bit, 1 4-shade, 2 8-bit, 3 16-bit, 4 32-bit, 5 2.5D, 6 3D),
`--no-story`, `--difficulty gentle|medium|hard`, `--frames N`, `--shot file.png`, `--shot-every N prefix`, `--seed N`, `--ending`, `--play` (skip the title), `--size W H`.
Example: `./build/upgrade --autoplay --two --fill --era 4` shows the 2.5D swing and then the full 3D stage.
The Memory Bar speed is `memory.fill_speed` in `assets/config.ini` (2.0 = twice the old speed).

## Debug switches (environment variables)
- `GAMEJAM_FRAMELOG=path` writes one line per frame ("frame draw_ms rss_kb") for timing and memory checks.
- `GAMEJAM_NOKIT=1` turns the Blender ground kit off (the old box ground is used): handy to time what the kit costs.
- The game still loads when model files in `assets/models` are missing: each model (Gran, Sam, clock, Forgetting, ground kit)
  falls back to the primitive shapes on its own.

## Regenerating the Blender art (bpy 5.2, `python3 -c "import bpy"` must work)
```
python3 tools/blender/make_models.py assets/models            # Gran, Sam, clock, Forgetting (prints the clock pivot constants:
                                                              #   copy them into Models3D::drawClockModel if they change)
python3 tools/blender/make_ground.py assets/models            # ground kit: ground_{grass,earth,deep}_N.glb (full)
python3 tools/blender/make_ground.py assets/models --lite     # groundlite_*.glb (half the triangles, used by the 2.5D meadow)
python3 -I tools/backdrop/make_era6_backdrop.py               # era 6 sky and far backdrop (assets/art/era6/bg_*.png)
```
`./build/assetgen assets` also rewrites assets/art/era5 and era6: restore them with `git checkout assets/art/era5 assets/art/era6`
unless you meant to change them. Textures and their licences are listed in `assets/textures/SOURCES.md`.
