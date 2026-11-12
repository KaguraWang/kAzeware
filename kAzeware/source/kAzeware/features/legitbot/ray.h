#pragma once
#include "../../utils/math/vector/vector.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace Legitbot {
    inline float Dot(const Vector_t& a, const Vector_t& b) {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    // Returns distance along a normalized ray, or infinity when it misses.
    inline float RayCapsule(const Vector_t& origin,
                            const Vector_t& direction,
                            float range,
                            const Vector_t& a,
                            const Vector_t& b,
                            float radius) {
        if (range <= 0.f || radius <= 0.f)
            return std::numeric_limits<float>::infinity();
        const Vector_t ba = b - a, oa = origin - a;
        // Long rays near a small capsule lose their intersection to float
        // cancellation. Include the represented direction length as well.
        const auto dot = [](const Vector_t& x, const Vector_t& y) {
            return double(x.x) * y.x + double(x.y) * y.y + double(x.z) * y.z;
        };
        const double dd = dot(direction, direction);
        if (dd <= 0.)
            return std::numeric_limits<float>::infinity();
        const double baba = dot(ba, ba), bard = dot(ba, direction), baoa = dot(ba, oa);
        const double rdoa = dot(direction, oa), oaoa = dot(oa, oa);
        const float projection = baba > 1e-8 ? static_cast<float>(std::clamp(baoa / baba, 0., 1.)) : 0.f;
        const Vector_t nearPoint = oa - ba * projection;
        if (Dot(nearPoint, nearPoint) <= radius * radius)
            return 0.f;
        double result = std::numeric_limits<double>::infinity();
        const double A = baba * dd - bard * bard, B = baba * rdoa - baoa * bard;
        const double C = baba * oaoa - baoa * baoa - double(radius) * radius * baba;
        const double discriminant = B * B - A * C;
        if (A > 1e-8f && discriminant >= 0.f) {
            const double t = (-B - std::sqrt(discriminant)) / A;
            const double axis = baoa + t * bard;
            if (t >= 0.f && t <= range && axis >= 0.f && axis <= baba)
                result = t;
        }
        for (const Vector_t center : {a, b}) {
            const Vector_t oc = origin - center;
            const double q = dot(direction, oc), h = q * q - dd * (dot(oc, oc) - double(radius) * radius);
            if (h < 0.f)
                continue;
            const double t = (-q - std::sqrt(h)) / dd;
            if (t >= 0.f && t <= range)
                result = (std::min)(result, t);
        }
        return static_cast<float>(result);
    }

    inline float TriggerCenterRadius(float capsuleRadius, bool multipoint, float scale) {
        // Half a unit is a small center tolerance, not the full capsule envelope.
        return (std::min)(capsuleRadius,
                          (std::max)(.5f, multipoint ? capsuleRadius * std::clamp(scale, 0.f, 100.f) * .0095f : 0.f));
    }

} // namespace Legitbot
