#pragma once

#include "../../utils/math/viewmatrix/viewmatrix.h"
#include "../../../../external/imgui/imgui.h"
#include <cstdint>

namespace Hitmarker {
    void Install();
    void Shutdown();
    void OnGameEvent(void* gameEvent);
    void Draw(const ViewMatrix& vm);

    // Call on AF/TR/aim fire - world mark uses this ray until bullet_impact arrives.
    // eye = shoot origin, fireAngles = punched view used for the pellet.
    void NoteLastFire(const Vector_t& eye, const QAngle_t& fireAngles);

    // ReportHit pattern addr for optional hook (0 if unresolved).
    uintptr_t ReportHitAddr();
} // namespace Hitmarker
