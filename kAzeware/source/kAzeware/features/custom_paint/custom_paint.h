#pragma once
#include <cstdint>
#include "../../../../external/imgui/imgui.h"
#include <array>
#include <vector>

namespace CustomPaint {

    // Build 14192: the econ_instance construction site has the loose-variable
    // vector in R8 and the weapon in RSI. Append copies 0x288-byte elements.
    // Field offsets and enum values match CompositeMaterialInputLooseVariable_t
    // in dumper/output/client_dll.json. Float components have separate bounds.
    constexpr int kLooseVarColor4 = 9;
    constexpr int kLooseVarFloat3 = 7;
    constexpr size_t kLooseVarSize = 0x288;
    constexpr size_t kOffName = 0x0;    // CUtlString name is at offset 0x0 in loose variable struct
    constexpr size_t kOffType = 0x40;   // int32
    constexpr size_t kOffColor4 = 0x8C; // uint32_t RGBA
    constexpr size_t kOffFloatX = 0x5C;
    constexpr size_t kOffFloatY = 0x68;
    constexpr size_t kOffFloatZ = 0x74;

    struct WeaponTintEntry {
        int def = 0;
        int paint = 0;
        ImVec4 cols[4];
    };

    struct GloveTintEntry {
        int def = 0;
        int paint = 0;
        ImVec4 col;
    };

    bool Init();
    void Shutdown();
    bool IsReady();

    // Per-skin tint storage - every skin (def+paint) has its own 4 colors. Picker shows/edits the selected skin only.
    void SetWeaponTint(int def, int paint, const ImVec4 cols[4]);
    bool GetWeaponTint(int def, int paint, ImVec4 out[4]);
    void SetGloveTint(int def, int paint, const ImVec4& col);
    bool GetGloveTint(int def, int paint, ImVec4& out);
    void ClearAllTints();

    std::vector<WeaponTintEntry> GetWeaponTintSnapshot();
    std::vector<GloveTintEntry> GetGloveTintSnapshot();

} // namespace CustomPaint
