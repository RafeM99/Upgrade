// Chunk.h: hand-made level chunks (ASCII files in assets/chunks) and a small
// generator that makes extra simple chunks so the endless level never repeats exactly.
//
// Chunk file format (brief section 10): first line is a tag header, then the grid.
//   # abilities=double_jump min_era=1 difficulty=2 coop=false weight=1
//   # abilities=none stages=1,2 difficulty=1 weight=2        (a chunk made for those graphics stages)
// min_era uses the old four-era scale (0 1-bit, 1 8-bit, 2 16-bit, 3 2.5D) and is mapped to the
// matching graphics stage; min_stage gives a stage (0..6) directly. A chunk with stages= is only
// placed in those stages, and is preferred there (level.stage_chunk_share).
// Legend: . empty  # solid  = cracked floor (ground pound)  1 / 2 platform only solid for P1 / P2
//         | wall (wall jump)  S Static Blob  G Glitch Flyer  B Fog Brute  F fragment
//         a / b Static Blob only P1 / P2 can see and hurt   P random power-up
// Grids are bottom-aligned into the 14-row level, so a file can be shorter than 14 rows.
// The first and last two columns should have ground in the bottom rows so chunks join up.
#pragma once
#include <cstdint>
#include <random>
#include <string>
#include <vector>

constexpr int LEVEL_H = 14;

struct Chunk {
    std::string name;
    int w = 0;
    std::vector<std::string> rows;   // LEVEL_H rows of w chars
    unsigned requires = 0;           // bit mask of Ability
    int minEra = 0;                  // first graphics stage (0..6) it may appear in
    unsigned stages = 0;             // bit mask of the stages it was made for, 0 = any
    int difficulty = 1;
    bool coop = false;
    int weight = 1;
    bool generated = false;
};

// Loads every *.txt in dir. Writes problems into `log`. Returns the count loaded.
int loadChunks(const std::string& dir, std::vector<Chunk>& out, std::string& log);

// Makes a fresh simple chunk for this era using only abilities in `abilityMask`.
Chunk generateChunk(std::mt19937& rng, int era, unsigned abilityMask, int difficulty);

// A flat chunk to start the run on.
Chunk startChunk();
