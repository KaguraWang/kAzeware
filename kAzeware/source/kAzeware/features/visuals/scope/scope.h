#pragma once

namespace Scope {
    // Custom scope crosshair lines when scoped (ImGui; replaces engine overlay).
    void DrawLines();

    // True only in-match, alive, scoped + zoomLevel>0 (not lobby stale flag).
    bool LocalScopedForOverlay();

    // Own scoped and unscoped FOV while zoom override is enabled.
    bool GetCustomZoomFov(float& outFov);

} // namespace Scope
