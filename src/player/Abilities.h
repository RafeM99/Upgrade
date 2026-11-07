// Abilities.h: the six unlockable abilities and who gets which at each era.
// Each ability is off until unlocked (brief section 7).
#pragma once
#include <cstring>

enum Ability { AB_DOUBLE_JUMP, AB_DASH, AB_GROUND_POUND, AB_WALL_JUMP, AB_GLIDE, AB_TIME_SLOW, AB_COUNT };

inline const char* abilityId(int a)
{
    static const char* ids[AB_COUNT] = {"double_jump", "dash", "ground_pound", "wall_jump", "glide", "time_slow"};
    return (a >= 0 && a < AB_COUNT) ? ids[a] : "";
}

inline int abilityFromId(const char* id)
{
    for (int a = 0; a < AB_COUNT; ++a)
        if (std::strcmp(abilityId(a), id) == 0) return a;
    return -1;
}

// The ability player `p` (0 or 1) unlocks on reaching graphics stage `era` (1..6), or -1.
// Stage 2 (8-bit) brings the attack button instead (config player.attack_from_era).
inline int abilityForEra(int p, int era)
{
    static const int table[2][7] = {{-1, AB_DOUBLE_JUMP, -1, AB_GROUND_POUND, -1, AB_GLIDE, -1},
                                    {-1, AB_DASH, -1, -1, AB_WALL_JUMP, -1, AB_TIME_SLOW}};
    return (era >= 1 && era <= 6 && p >= 0 && p <= 1) ? table[p][era] : -1;
}
