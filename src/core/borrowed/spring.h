#pragma once
// Adapted from SpringValue in the user-supplied dynamic-island-for-windows 1.4.0.
// Copyright (c) 2026 devcode90, MIT. See third_party/dynamic-island/LICENSE.
#include <algorithm>
#include <cmath>

struct IslandSpring {
    float value = 0;
    float velocity = 0;
    float target = 0;
    void reset(float v) { value = target = v; velocity = 0; }
    void step(float totalDt, float stiffness = 280, float damping = 24) {
        // Clamp long pauses; the original fixed substep integration is retained.
        totalDt = std::clamp(totalDt, 0.0f, 0.05f);
        while (totalDt > 0) {
            const float dt = std::min(totalDt, 0.0005f);
            const float acceleration = -stiffness * (value - target) - damping * velocity;
            velocity += acceleration * dt;
            value += velocity * dt;
            totalDt -= dt;
        }
        if (std::fabs(value - target) < 0.01f && std::fabs(velocity) < 0.01f) {
            value = target; velocity = 0;
        }
    }
};
