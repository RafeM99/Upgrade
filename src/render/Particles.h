// Particles.h: simple CPU particles (sparks, dust, fog wisps) in tile units.
#pragma once
#include "raylib.h"
#include <vector>

struct Particle {
    float x, y, vx, vy, life, maxLife, size, grav;
    Color color;
};

class Particles {
public:
    void burst(float x, float y, int n, Color c, float speed, float life, float size, float grav = 0);
    void add(const Particle& p) { ps_.push_back(p); }
    void update(float dt);
    void clear() { ps_.clear(); }
    const std::vector<Particle>& all() const { return ps_; }

private:
    std::vector<Particle> ps_;
    unsigned seed_ = 1;
    float rnd();
};
