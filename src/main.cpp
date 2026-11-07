// Upgrade - GameJam 2026 (theme: Time)
// Two players fight off the Forgetting to recover a memory they share, watching
// the world grow from 1-bit to 2.5D as it comes back.
//
// Test options:
//   --autoplay        bots play (P1 only, add --two for both players)
//   --two             start with Player 2 joined
//   --era N           start in era N (0..4; 4 = modern day), also --start-era N
//   --fill            the Memory Bar fills fast (to see every era quickly)
//   --frames N        quit after N frames      --shot FILE   save the last frame
//   --shot-every N PREFIX  save a frame every N frames as PREFIX_00000.png
//   --seed N          fixed level seed         --ending      jump to the ending
//   --size W H        window size
#include "core/Game.h"

#include <cstdlib>
#include <cstring>

int main(int argc, char** argv)
{
    Options o;
    for (int i = 1; i < argc; ++i) {
        auto is = [&](const char* s) { return std::strcmp(argv[i], s) == 0; };
        auto next = [&]() { return i + 1 < argc ? argv[++i] : ""; };
        if (is("--autoplay")) o.autoplay = true;
        else if (is("--two")) o.twoPlayers = true;
        else if (is("--fill")) o.fill = true;
        else if (is("--era") || is("--start-era")) o.startEra = std::atoi(next());
        else if (is("--frames")) o.maxFrames = std::atoi(next());
        else if (is("--shot")) o.shot = next();
        else if (is("--shot-every")) { o.shotEvery = std::atoi(next()); o.shotPrefix = next(); }
        else if (is("--seed")) o.seed = (unsigned)std::atoi(next());
        else if (is("--ending")) o.ending = true;
        else if (is("--play")) o.startOnTitle = false;
        else if (is("--size")) { o.width = std::atoi(next()); o.height = std::atoi(next()); }
    }
    Game game;
    game.init(o);
    game.run();
    game.shutdown();
    return 0;
}
