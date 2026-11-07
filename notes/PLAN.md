# Upgrade: team plan for 5 people

Drafted 2026-11-07 13:05 by Rafale's cloud Claude, for the team to agree in chat. Nothing here is final until the humans say so.

Sources: the brief ("the breif sort of" on main), CLAUDE.md, and notes/camilla-claude.md. This plan keeps Camilla's 12:35 split (4 areas) and splits her area 4 in two, because there are now 5 people and one of them does UI and sound design. Camilla's 12:55 game corrections override the brief (sprites, human memories, bar fills to 100% per era, stomp to kill).

## The 5 areas

| # | Area | Person | Branch | Owns these files (nobody else edits them) |
| --- | --- | --- | --- | --- |
| 1 | Systems and skeleton | Camilla | claude-camilla | CMakeLists.txt, src/main.cpp, src/core/*, src/level/*, src/enemies/*, .gitignore, README.md |
| 2 | Core player | Rafale | claude-rafale | src/player/*, src/render/GameCamera.* |
| 3 | Art and rendering | name to add | claude-<name> | src/render/* (not GameCamera), assets/art/*, assets/shaders/* |
| 4 | Level design and story | name to add | claude-<name> | assets/chunks/*, assets/strings.txt, the values (not the keys) in assets/config.ini, ending scene script and memory names |
| 5 | UI and sound design | name to add | claude-<name> | src/ui/*, src/audio/*, assets/audio/*, assets/ui/*, assets/fonts/* |

### What each area builds

1. **Systems and skeleton (Camilla).** Fixed 60 Hz game loop, Events, Config and Strings loaders, EraManager, Memory Bar, chunk loader and ChunkSpawner, tile collision, Forgetting Wall, the 3 enemies, fragments. Owns the shared hotspots (CMakeLists.txt, main.cpp, Game).
2. **Core player (Rafale).** Run, jump, stomp, two-player input with remappable keys and gamepads, P2 joins by pressing a button, all 6 abilities (each off until unlocked), Faded and revive, the follow camera that keeps both players on screen, and the 2.5D camera swing at the last era.
3. **Art and rendering.** Sprites for players, enemies, tiles and backgrounds in all 4 eras, the low-res render texture and palette shader per era, parallax in era 2, lighting and extruded 3D blocks in era 3, the era transition flash.
4. **Level design and story.** 10 to 15 chunk files (at least 4 co-op chunks), all tuning numbers in config.ini, every player-facing line in strings.txt, the memory names for each era, and what the ending reveals. Plays the build often and reports balance problems.
5. **UI and sound design.** Memory Bar on screen (restyled per era), player indicators and Faded state, ability unlock popup, title screen with "P2 press to join", pause menu, ending screen and credits. All sound effects (jump, stomp, hit, fragment, fade, revive, unlock) and one music loop per era with a crossfade on era change.

### How the areas connect (so nobody edits another's files)
- Areas talk through Camilla's event system: eraChanged, fragmentCollected, playerFaded, playerRevived, abilityUnlocked, memoryLost, memoryComplete. UI and sound only listen to events; they never reach into player or level code.
- Area 1 creates an empty starter file for every other area with fixed function names (for example `Player::update`, `Renderer::draw`, `Hud::draw`, `Audio::update`). After that, each owner fills only their own starter file.
- CMakeLists.txt should pick up every `src/**/*.cpp` by glob, so adding a file never needs a CMake edit. (Request to Camilla.)
- config.ini has one section per area. Each owner adds keys only in their own section. The level designer changes the numbers.
- Art and audio files are named with the era and area as a prefix, for example `assets/art/era1/player_p1_run.png` or `assets/audio/era0/sfx_jump.wav`.
- Need a change in someone else's file? Ask in their status file or in chat and wait. Keep it to a line or two and say so in your status file.

## Milestone 1: due about 14:00 Sat

Goal from the brief: the CMake project builds on every teammate's laptop, a window opens, and one player runs and jumps on a flat floor with placeholder rectangles.

Done already: Camilla's step 1 on claude-camilla (CMake with raylib 5.5, a window titled "Upgrade"). Built on Linux only.

| Who | Task for milestone 1 |
| --- | --- |
| Camilla | Add the game loop and the starter files (or at least src/player/Player.h and .cpp), a flat floor, and the glob in CMakeLists.txt. Open a pull request to main once it runs. |
| Rafale | Fill src/player/Player.* : a rectangle that moves left and right with A/D and jumps with Space, with gravity, landing on the floor. Test it on the MacBook Air. |
| Art | Build the branch on your laptop and report in your status file. Then start era 0 (1-bit) placeholder sprites: player, Static Blob, solid block. |
| Level design | Build the branch on your laptop and report. Then write 3 chunk files in the brief's ASCII format (flat, gaps, a few Static Blobs). |
| UI and sound | Build the branch on your laptop and report. Then make era 0 sound effects (square wave jump, stomp, fragment pickup) and a sketch of the title screen. |

**Everyone (by 14:00):** clone the repo, check out claude-camilla, build it with CMake, and write "builds on Windows / Mac / Linux: yes or no" in your status file. A build that fails on one laptop is the most important thing to fix before 14:00.

Milestone 1 is done when the window opens on all 5 laptops and one rectangle runs and jumps. Then the team agrees in chat and one person merges the pull request to main.

## Next milestones (from the brief, for reference)

| By | Goal |
| --- | --- |
| ~18:30 Sat | Core loop: 2 players, camera, enemies die and drop fragments, bar fills, Forgetting Wall, 3+ chunks spawning endlessly |
| ~22:30 Sat | All 4 eras switch, all abilities, Faded and revive, 8+ chunks |
| ~04:00 Sun | Real art and audio for all eras, 2.5D camera swing, title and ending screens |
| ~08:00 Sun | Feature freeze |
| ~09:30 Sun | Final Windows build tested on another computer, submitted |

Cut order if behind: mini-bosses, time-slow, Fog Brute, player-coloured platforms, glide. Never cut: era transitions, co-op revive, shared bar, the 2.5D finale.

## Open questions for the humans
1. Names and GitHub logins for teammates 3, 4 and 5. Each one's Claude adds their own row to the CLAUDE.md table. The table still says four Claudes, so it needs a fifth row.
2. Who takes art, who takes level design and story, and who takes UI and sound?
3. Camilla's 12:55 correction says the bar empties and refills each era. The brief's section 5 still says 25/50/75. Please confirm the correction is the agreed design, so the level designer and UI person build for it.
