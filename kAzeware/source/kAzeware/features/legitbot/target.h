#pragma once
#include <array>
#include <cstdint>
#include "legit_math.h"
#include "../../../cs2/entity/handle.h"
#include "../../utils/math/vector/vector.h"

class C_CSPlayerPawn;

namespace Legitbot {
    enum class Priority { Crosshair, Distance, Health };

    struct SelectionSettings {
        float verticalFov = 5.f, horizontalFov = 5.f;
        std::array<bool, 8> groups{true, false, false, false, false, false, false, false};
        std::array<int, 8> order{0, 1, 2, 3, 4, 5, 6, 7};
        Priority priority = Priority::Crosshair;
        int minDamage = 0;
        int fallbackMode = 0, preferredStudio = -1, preferredPoint = -1;
        CBaseHandle preferred{};
        float lockFov = 0.f, stickiness = 0.f;
        bool lock = false, dynamicFov = false;
        bool teamCheck = true, targetVisibleCheck = true, visibleOnly = true, forceBody = false, multipoint = false,
             smokeCheck = false;
        std::array<float, 8> pointScales{50.f, 50.f, 50.f, 50.f, 50.f, 50.f, 50.f, 50.f};
    };

    struct Target {
        CBaseHandle handle{};
        Vector_t point{};
        LegitMath::Angles angle{};
        float score = 0.f, verticalFov = 0.f, horizontalFov = 0.f;
        int group = -1;
        int studioIndex = -1, pointIndex = -1;

        explicit operator bool() const {
            return handle.valid();
        }
    };

    Target SelectTarget(C_CSPlayerPawn* local, LegitMath::Angles view, const SelectionSettings& settings);
} // namespace Legitbot
