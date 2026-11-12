#pragma once
#include <algorithm>
#include <cmath>

namespace Rage::AutoAim {
    // The policy selects a reliability goal, not a fixed HC percentage. Actual
    // HC also depends on damage, required hits and the weapon/pose spread ceiling.
    inline float Reliability(int mode) {
        constexpr float goals[]{.90f, .98f, .70f};
        return goals[std::clamp(mode, 0, 2)];
    }

    inline int RequiredHits(int health, float damage, int mode, int samples, int attainable) {
        if (health <= 0 || !std::isfinite(damage) || damage <= 0.f || attainable <= 0)
            return samples + 1;
        const float goal = Reliability(mode);
        const int bullets = (std::max)(1, int(std::ceil(health / damage)));
        // A multi-hit kill requires higher per-shot reliability. Do not assume a
        // second DT bullet will be available to retry a missed lethal first shot.
        const float probability = std::pow(goal, 1.f / bullets);
        return (std::min)(attainable, (std::max)(1, int(std::ceil(probability * samples))));
    }

    inline float PointRadius(float radius, float distance, float cone) {
        if (!(radius > 0.f) || !std::isfinite(cone) || cone < 0.f)
            return 0.f;
        // Largest offset whose selected spread quantile stays inside the
        // capsule's center sphere; manual scales retain their existing formula.
        return std::clamp(radius - distance * cone, 0.f, radius * .95f);
    }
} // namespace Rage::AutoAim
