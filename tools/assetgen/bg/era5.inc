// Background scene for stage 5, included into SpriteGen.cpp just before bgLayer().
// Return true when this file painted the layer (bgLayer then returns), false to fall back to
// the older scene in bgLayer().
bool bgEra5(Scene& sc, int layer)
{
    (void)sc; (void)layer;
    return false;
}
