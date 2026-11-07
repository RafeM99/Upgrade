#include "render/Particles.h"
#include <algorithm>
#include <cmath>

float Particles::rnd()
{
    seed_ = seed_ * 1664525u + 1013904223u;
    return (seed_ >> 8) / 16777216.0f;
}

void Particles::burst(float x, float y, int n, Color c, float speed, float life, float size, float grav)
{
    for (int i = 0; i < n; ++i) {
        float a = rnd() * 6.2831853f, s = speed * (0.3f + 0.7f * rnd());
        ps_.push_back({x, y, std::cos(a) * s, std::sin(a) * s, life * (0.6f + 0.4f * rnd()), life, size * (0.6f + 0.6f * rnd()), grav, c});
    }
    if (ps_.size() > 3000) ps_.erase(ps_.begin(), ps_.begin() + (ps_.size() - 3000));
}

void Particles::update(float dt)
{
    for (auto& p : ps_) {
        p.vy += p.grav * dt;
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        p.vx *= 1.0f - 1.5f * dt;
        p.life -= dt;
    }
    ps_.erase(std::remove_if(ps_.begin(), ps_.end(), [](const Particle& p) { return p.life <= 0; }), ps_.end());
}
