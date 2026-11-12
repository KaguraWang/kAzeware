#pragma once
#include <algorithm>
#include <cmath>

namespace MovementBrake {
    struct Pulse {
        float x = 0, y = 0, gain = 0, duty = 0;
    };

    // Shared by prediction and the command writer. Native subtick boundaries have
    // 1/64-tick precision: each of our 16 slices contains four usable time units.
    inline Pulse Solve(
        float vx, float vy, float gx, float gy, float acceleration, float cap, float dt, bool limited, int quanta = 4) {
        // Combat targets are speed ceilings. Above that ceiling, spend the full
        // available input opposing velocity; turning toward a sideways walking
        // direction first wastes braking authority and delays the HC threshold.
        // Once below it, normal steering toward the requested walk vector resumes.
        if (limited) {
            const float speed2 = vx * vx + vy * vy, goal2 = gx * gx + gy * gy;
            if (speed2 > goal2) {
                const float scale = goal2 > 0.f ? std::sqrt(goal2 / speed2) : 0.f;
                gx = vx * scale;
                gy = vy * scale;
            }
        }
        const float dx = gx - vx, dy = gy - vy, length = std::hypot(dx, dy);
        if (length <= .0001f)
            return {};
        Pulse p{dx / length, dy / length};
        const float along = vx * p.x + vy * p.y;
        p.gain = (std::min)(acceleration * dt, (std::max)(0.f, cap - along));
        const float requested = p.gain > 0.f ? std::clamp(length / p.gain, 0.f, 1.f) : 0.f;
        p.duty = limited ? std::floor(requested * quanta) / quanta : requested;
        if (limited && along < 0.f) {
            const float rounded = std::ceil(requested * quanta) / quanta;
            const float x = vx + p.x * p.gain * rounded, y = vy + p.y * p.gain * rounded;
            if (x * x + y * y < vx * vx + vy * vy)
                p.duty = rounded;
        }
        return p;
    }
} // namespace MovementBrake
