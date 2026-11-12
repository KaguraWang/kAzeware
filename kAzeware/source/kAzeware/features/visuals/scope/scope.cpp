#include "scope.h"
#include "../../../hooks/hooks.h"
#include "../../../config/config.h"
#include "../../../interfaces/interfaces.h"
#include "../../../../../external/imgui/imgui.h"
#include "../../../../cs2/entity/C_CSPlayerPawn/C_CSPlayerPawn.h"
#include "../../../../cs2/entity/C_CSWeaponBase/C_CSWeaponBase.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

// DrawScopeOverlay out-struct (client.dll @ 0x1808A0740)
// +0
// blend / intensity
// +4 bool draw
// +8 float blur strength (0..1)
// +12 float lens size X
// +16 float lens size Y
// +20 int style
// +24 int screen metric

namespace Scope {
    namespace {

        // Menu/lobby still has a pawn sometimes; m_bIsScoped can stick true after disconnect.
        bool InMatch() {
            if (!I::EngineClient)
                return false;
            bool ok = false;
            __try {
                ok = I::EngineClient->in_game() && I::EngineClient->connected();
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
            return ok;
        }

        bool LocalAlivePawn(C_CSPlayerPawn*& out) {
            out = nullptr;
            if (!InMatch())
                return false;
            C_CSPlayerPawn* local = H::SafeLocalPlayer();
            if (!local)
                return false;
            int hp = 0;
            std::uint8_t life = 1;
            __try {
                hp = local->m_iHealth();
                life = local->m_lifeState();
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
            if (hp <= 0 || hp > 200 || life != 0)
                return false;
            out = local;
            return true;
        }

        bool LocalScoped() {
            C_CSPlayerPawn* local = nullptr;
            if (!LocalAlivePawn(local))
                return false;
            bool scoped = false;
            int zl = 0;
            __try {
                scoped = local->m_bIsScoped();
                // Require live zoom - stale m_bIsScoped alone draws lines in lobby
                if (C_CSWeaponBase* wep = local->GetActiveWeapon())
                    zl = wep->m_zoomLevel();
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
            return scoped && zl > 0;
        }

    } // namespace

    bool GetCustomZoomFov(float& outFov) {
        static C_CSPlayerPawn* zoomPawn = nullptr;
        static C_CSWeaponBase* zoomWeapon = nullptr;
        static int lastZoom = 0;
        static float exitProgress = 0.f;
        C_CSPlayerPawn* local = nullptr;
        if (!Config::local_visuals || !Config::scope_zoom_fov || !LocalAlivePawn(local)) {
            lastZoom = 0;
            return false;
        }
        __try {
            auto* weapon = local->GetActiveWeapon();
            if (!weapon) {
                lastZoom = 0;
                return false;
            }
            if (local != zoomPawn || weapon != zoomWeapon)
                lastZoom = 0;
            zoomPawn = local;
            zoomWeapon = weapon;
            const int level = local->m_bIsScoped() ? weapon->m_zoomLevel() : 0;
            if (level > 0) {
                lastZoom = level;
                exitProgress = 0.f;
                outFov = std::clamp(level == 1 ? Config::scope_fov_1 : Config::scope_fov_2, 1.f, 150.f);
                return true;
            }
            const auto cameraOffset = SchemaFinder::Get(hash_32_fnv1a_const("C_BasePlayerPawn->m_pCameraServices"));
            if (!cameraOffset)
                return false;
            void* camera = Mem::ReadFieldOr<void*>(local, cameraOffset, nullptr);
            if (!camera)
                return false;
            // Native GetRenderFov obtains the unzoomed FOV from camera vtable +0x120.
            const float nativeHip = M::CallVFunc<float, 36>(camera);
            if (!std::isfinite(nativeHip) || nativeHip <= 0.f)
                return false;
            const float customHip = Config::fovEnabled ? Config::fov : nativeHip;
            outFov = customHip;
            // Keep owning the unscoped projection too. A shot can restart the native
            // zoom blend after an earlier call has already reached the hip FOV.
            if (!lastZoom)
                return true;
            const auto startOffset =
                SchemaFinder::Get(hash_32_fnv1a_const("CCSPlayerBase_CameraServices->m_iFOVStart"));
            auto original = H::GetRenderFov.GetOriginal();
            if (!startOffset || !original)
                return true;
            const float nativeStart = static_cast<float>(Mem::ReadFieldOr<std::uint32_t>(camera, startOffset, 0));
            const float nativeFov = original(camera);
            if (!std::isfinite(nativeFov) || std::fabs(nativeHip - nativeStart) < 0.01f) {
                lastZoom = 0;
                return true;
            }
            // Prediction/native FOV-start rewrites must not reverse a custom exit.
            exitProgress =
                (std::max)(exitProgress, std::clamp((nativeFov - nativeStart) / (nativeHip - nativeStart), 0.f, 1.f));
            const float customStart = std::clamp(lastZoom == 1 ? Config::scope_fov_1 : Config::scope_fov_2, 1.f, 150.f);
            outFov = std::lerp(customStart, customHip, exitProgress);
            if (exitProgress >= 1.f)
                lastZoom = 0;
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            lastZoom = 0;
            return false;
        }
    }

} // namespace Scope

namespace H {
    static std::int64_t __fastcall hkDrawScopeOverlayBody(void* a1, void* a2) {
        auto original = H::DrawScopeOverlay.GetOriginal();
        std::int64_t result = 0;
        if (original) {
            __try {
                result = original(a1, a2);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return 0;
            }
        }

        if (!a2)
            return result;

        // Only strip stock overlay in-match while custom lines own the cross
        if (!(Config::local_visuals && Config::scope_custom_lines) || !Scope::LocalScopedForOverlay())
            return result;

        __try {
            auto* out = reinterpret_cast<std::uint8_t*>(a2);
            out[4] = 0; // draw = false - kill bars/texture/stock lines
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            TW_SEH_CATCH("scope.disableStockOverlay");
        }

        return result;
    }
} // namespace H

std::int64_t __fastcall H::hkDrawScopeOverlay(void* a1, void* a2) {
    H::HookScopeGuard guard;
    guard.arm();
    if (H::Hooks::s_unloading.load(std::memory_order_acquire)) {
        auto original = H::DrawScopeOverlay.GetOriginal();
        if (original)
            return original(a1, a2);
        return {};
    }
    return H::hkDrawScopeOverlayBody(a1, a2);
}

// Exposed for hook - same gates as DrawLines
bool Scope::LocalScopedForOverlay() {
    return LocalScoped();
}

void Scope::DrawLines() {
    if (!(Config::local_visuals && Config::scope_custom_lines))
        return;
    if (!LocalScoped())
        return;

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    if (!dl)
        return;

    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    if (ds.x < 8.f || ds.y < 8.f)
        return;

    const float cx = ds.x * 0.5f;
    const float cy = ds.y * 0.5f;

    const float size = std::clamp(Config::scope_line_size, 0.f, 1.f);
    const float gap = std::clamp(Config::scope_line_gap, 0.f, 80.f);
    // Hairline range: 0.1 .. 6 (was min 0.5 + heavy outline = always thick)
    const float th = std::clamp(Config::scope_line_thickness, 0.1f, 6.f);

    const float maxArmX = (std::max)(0.f, cx - gap);
    const float maxArmY = (std::max)(0.f, cy - gap);
    const float armX = maxArmX * size;
    const float armY = maxArmY * size;
    if (armX < 1.f && armY < 1.f)
        return;

    const ImVec4& c = Config::scope_line_color;
    const int a = static_cast<int>(std::clamp(c.w, 0.f, 1.f) * 255.f);
    if (a < 8)
        return;
    const ImU32 col = IM_COL32(static_cast<int>(std::clamp(c.x, 0.f, 1.f) * 255.f),
                               static_cast<int>(std::clamp(c.y, 0.f, 1.f) * 255.f),
                               static_cast<int>(std::clamp(c.z, 0.f, 1.f) * 255.f),
                               a);

    // Outline only when thick enough - hairlines stay 1px
    const bool doOutline = th >= 0.75f;
    const float oth = th + 0.8f;
    const ImU32 outline = IM_COL32(0, 0, 0, (a * 2) / 3);

    auto hLine = [&](float x0, float x1) {
        if (doOutline)
            dl->AddLine(ImVec2(x0, cy), ImVec2(x1, cy), outline, oth);
        dl->AddLine(ImVec2(x0, cy), ImVec2(x1, cy), col, th);
    };
    auto vLine = [&](float y0, float y1) {
        if (doOutline)
            dl->AddLine(ImVec2(cx, y0), ImVec2(cx, y1), outline, oth);
        dl->AddLine(ImVec2(cx, y0), ImVec2(cx, y1), col, th);
    };

    if (armX >= 1.f) {
        hLine(cx - gap - armX, cx - gap);
        hLine(cx + gap, cx + gap + armX);
    }
    if (armY >= 1.f) {
        vLine(cy - gap - armY, cy - gap);
        vLine(cy + gap, cy + gap + armY);
    }
}
