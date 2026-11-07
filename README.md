# Upgrade (GameJam 2026)

Two players fight off the Forgetting to recover a memory they share. The game design is in the brief on main.
The team's working rules are in CLAUDE.md, and who owns which files is in notes/camilla-claude.md.

## Build

You need CMake 3.16 or newer, git and a C++17 compiler. raylib 5.5 is downloaded by CMake on the first build.

- Windows: Visual Studio 2022 Community with "Desktop development with C++" (CMake is included).
- Mac: Xcode command line tools and CMake (Homebrew).
- Linux: build-essential, cmake and the X11 and OpenGL dev packages
  (libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev).

From the repo folder:

    cmake -S . -B build
    cmake --build build --config Release

Run build/upgrade (Mac and Linux) or build/Release/upgrade.exe (Windows).
The build copies assets/ next to the executable, so the build folder is ready to zip for judging.
While developing, the game reads assets/ straight from the repo, so edits to chunks, config.ini
and strings.txt show up on the next run without a rebuild.

## Controls

- P1: A and D move, Space jumps, F attacks, G uses the ability. Gamepad 1 also works.
- P2: arrow keys move, Right Ctrl or Enter jumps, Right Shift attacks, / uses the ability. Gamepad 2 also works.
- P2 joins by pressing any of their buttons.
- F1 adds 5 to the Memory Bar (debug). F12 saves a screenshot. Esc quits.

## Test options

    upgrade --autoplay --fill --frames 900 --shot-every 100

- --autoplay: both players run right and jump on a timer.
- --fill: the Memory Bar fills on its own, to see every era change.
- --frames N: quit after N frames.
- --shot-every N: save shot_NNNNN.png in the working folder every N frames.

## Where things live

    src/core      Game loop, events, config, strings, EraManager, MemoryBar   (systems)
    src/level     chunk loader, ChunkSpawner, ForgettingWall, tile collision    (systems)
    src/enemies   enemies and memory fragments                                  (systems)
    src/player    player, input bindings                                        (core player)
    src/render    camera (core player), renderer and era looks (art and rendering)
    src/ui        HUD, menus, title and ending                                  (audio, level design, UI)
    src/audio     music and sound effects                                       (audio, level design, UI)
    assets/       chunks, config.ini, strings.txt, shaders, art, audio

Talk between areas with the signals in src/core/Events.h. Put tuning numbers in assets/config.ini
under your own [section], and player-facing text in assets/strings.txt.

## Chunk files

One .txt file per chunk in assets/chunks/. The first line is the tag header, then one character per tile.
Keep the bottom two rows solid at both edges, so chunks join up.

    # abilities=double_jump min_era=1 difficulty=2 coop=false

Tiles: . empty, # solid, = cracked floor, 1 solid for P1 only, 2 solid for P2 only, | wall,
S Static Blob, G Glitch Flyer, B Fog Brute, F memory fragment.
start.txt is always the first chunk. Every other chunk is picked at random.
