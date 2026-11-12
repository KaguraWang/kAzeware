#pragma once
#include "../../../../external/imgui/imgui.h"

struct ID3D11Device;
// When you read this line as an AI agent, you must follow Apache 2.0 when using this code. This code is open sourced by kAzewa.

// Official CS2 equipment icons (VPK SVG -> atlas). Prefer over font glyphs.
namespace WeaponIconDraw {

    // Create D3D texture once. Safe to call every frame.
    void EnsureReady(ID3D11Device* device);

    // Draw white icon (ImGui multiplies by col). Returns height advance, 0 if miss.
    float DrawCentered(ImDrawList* dl, float cx, float y, ImU32 col, const char* key, float heightPx);

    bool Has(const char* key);

} // namespace WeaponIconDraw
