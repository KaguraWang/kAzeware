#include "../config/language.h"
#include "hud.h"
#include "../../../external/imgui/imgui.h"
#include "../config/config.h"
#include "../hooks/hooks.h"
#include "../keybinds/keybinds.h"
#include "../features/widgets/widgets.h"
#include "../features/notify/notify.h"
#include "../features/visuals/scope/scope.h"
#include "../utils/memory/memsafe/memsafe.h"
#include "../utils/console/console.h"
#include <ctime>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <DirectXMath.h>
#include <Windows.h>

Hud::Hud() {}

// CS2 GetRenderFov is horizontal. A 3D cone of `fovDegrees` around the look
// axis projects to r = (W/2) * tan(aim/2) / tan(hFov/2) at screen center.
float CalculateFovRadius(float fovDegrees, float screenWidth, float screenHeight, float gameHorizFov) {
    (void)screenHeight;
    float hFov = gameHorizFov;
    if (!std::isfinite(hFov) || hFov < 1.f || hFov > 179.f)
        hFov = 90.f;
    if (!std::isfinite(fovDegrees) || fovDegrees <= 0.01f)
        return 0.f;
    const float aimHalf = fovDegrees * (DirectX::XM_PI / 180.0f) * 0.5f;
    const float camHalf = hFov * (DirectX::XM_PI / 180.0f) * 0.5f;
    const float denom = std::tan(camHalf);
    if (!(denom > 1e-6f) || !std::isfinite(denom))
        return 0.f;
    const float radius = std::tan(aimHalf) * (screenWidth * 0.5f) / denom;
    return std::isfinite(radius) ? radius : 0.f;
}

// Scan FOV is view+punch. With visual recoil the camera is already punched
// so screen center matches. With remove-recoil the camera is raw aim - shift
// the ring onto the bullet direction so it lines up with the scan cone.

void RenderFovCircle(ImDrawList* drawList,
                     float fov,
                     ImVec2 screenCenter,
                     float screenWidth,
                     float screenHeight,
                     float thickness,
                     const ImVec4& col) {
    if (fov <= 0.01f)
        return;
    float radius = CalculateFovRadius(fov, screenWidth, screenHeight, H::g_flActiveFov);
    if (!std::isfinite(radius) || radius < 1.f)
        return;
    const uint32_t color = ImGui::ColorConvertFloat4ToU32(col);
    drawList->AddCircle(screenCenter, radius, color, 64, thickness);
}

void Hud::render() {
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    if (!drawList)
        return;

    // Widgets & FOV circles require an active game session - never render in main menu/lobby or map transitions.
    const bool inSession = H::SessionEntityReady() && !H::SessionMapLeaving() && !H::SessionPostMatch();
    if (inSession) {
        __try {
            Widgets::Render();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Con::Seh("Hud.widgets", GetExceptionCode());
        }
        __try {
            Scope::DrawLines();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Con::Seh("Hud.scope", GetExceptionCode());
        }
    }
    __try {
        Notify::Render();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Con::Seh("Hud.notify", GetExceptionCode());
    }
}
