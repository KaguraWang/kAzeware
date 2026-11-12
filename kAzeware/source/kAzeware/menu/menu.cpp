#include "../features/ragebot/rage.h"
#include "../config/translated_ui.h"
#include "menu.h"
#include "../../../external/imgui/imgui_internal.h"
#include "../config/config.h"
#include "../config/language.h"

#include <iostream>
#include <vector>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <cmath>
#include "../config/configmanager.h"

#include "../keybinds/keybinds.h"
#include "../features/grenade_helper/grenade_helper.h"
#include "../features/legitbot/bindings.h"
#include "../features/legitbot/profiles.h"
#include "../features/hitsound/hitsound.h"
#include "../features/world/skybox.h"
#include "../features/skinchanger/skin_menu.h"
#include <string>

#include "../utils/logging/log.h"
#include "../utils/console/console.h"
#include "../features/visuals/assets/undefeated_font.hpp"
#include "icons/fa_solid_font.hpp"

ImFont* g_WeaponIconFont = nullptr;
ImFont* g_MenuIconFont = nullptr;
ImFont* g_MenuHeadingFont = nullptr;

// Font Awesome 6 Free solid codepoints (UTF-8) for sidebar tabs
namespace MenuTabIcon {
    // subset glyphs: crosshairs f05b, eye f06e, folder-open f07c, sliders f1de
    static const char* kAim = "\xef\x81\x9b";     // U+F05B
    static const char* kVisuals = "\xef\x81\xae"; // U+F06E
    static const char* kSkins = "\xef\x81\xbc";   // U+F07C folder-open (subset has no palette glyph)
    static const char* kMisc = "\xef\x87\x9e";    // U+F1DE
    static const char* kConfig = "\xef\x81\xbc";  // U+F07C
    static const char* const kAll[6] = {kAim, kAim, kVisuals, kSkins, kMisc, kConfig};
} // namespace MenuTabIcon

#include "menu_ui.h"

// Thin layout facade - chrome lives in menu_ui.*.
namespace ui {
    using MenuUI::Accent;
    using MenuUI::TextMuted;
    using MenuUI::TextBright;
    using MenuUI::WithA;
    using MenuUI::ToU32;
    using MenuUI::AccentU32;
    using MenuUI::BorderU32;
    using MenuUI::DrawWindowFrame;

    static void SectionLabel(const char* label) {
        MenuUI::Section(label);
    }

    static void BeginCard(const char* id, float width, bool autoY = false) {
        MenuUI::BeginCard(id, width, autoY);
    }

    static void EndCard() {
        MenuUI::EndCard();
    }

    static void BeginStrip(const char* id) {
        MenuUI::BeginStrip(id);
    }

    static void EndStrip() {
        MenuUI::EndStrip();
    }

    static bool
    SliderFull(const char* label, const char* id, float* v, float vmin, float vmax, const char* fmt = "%.3f") {
        return MenuUI::Slider(label, id, v, vmin, vmax, fmt);
    }

    static bool SliderInt(const char* label, const char* id, int* v, int vmin, int vmax, const char* fmt = "%d") {
        return MenuUI::SliderInt(label, id, v, vmin, vmax, fmt);
    }

    static bool ComboFull(const char* label, const char* id, int* cur, const char* const items[], int count) {
        return MenuUI::Combo(label, id, cur, items, count);
    }

    static bool FeatureToggle(const char* label, bool* v, const char* tip = nullptr) {
        return MenuUI::FeatureToggle(label, v, tip);
    }

    static bool Checkbox(const char* label, bool* v) {
        return MenuUI::Checkbox(label, v);
    }

    static void PopupTitle(const char* title) {
        MenuUI::PopupTitle(title);
    }

    static bool NavButton(const char* label, const char* iconUtf8, bool selected, const ImVec2& size, int index = 0) {
        return MenuUI::NavButton(label, iconUtf8, selected, size, index);
    }

    static bool NavButton(const char* label, bool selected, const ImVec2& size, int index = 0) {
        return MenuUI::NavButton(label, selected, size, index);
    }

    static void SubNav(const char* const* labels, int count, int* selected) {
        MenuUI::SubNav(labels, count, selected);
    }

} // namespace ui

void ApplyImGuiTheme() {
    MenuUI::ApplyTheme();
}

namespace {
    float g_builtFontSize = -1.f;

    void AddMenuFonts(float uiFontSize) {
        ImGuiIO& io = ImGui::GetIO();
        uiFontSize = std::clamp(uiFontSize, 12.f, 24.f);

        static const ImWchar kChineseRanges[] = {
            0x0020, 0x0FFF, 0x2000, 0x206F, 0x3000, 0x30FF, 0x3400, 0x4DBF, 0x4E00, 0x9FFF, 0};
        ImFontConfig uiCfg{};
        uiCfg.OversampleH = 1;
        uiCfg.OversampleV = 1;
        uiCfg.PixelSnapH = true;

        // Robust CJK font chain: try every common Windows font that actually
        // contains CJK glyphs before falling back to fonts without any Chinese.
        // If the default chain (msyh -> arial -> ImGui default) loses and the
        // game/Windows lacks msyh, every Chinese label renders as blank space
        // while icons and ASCII stay visible: "sidebar flickers, content blank".
        static const char* kCjkFonts[] = {"C:\\Windows\\Fonts\\msyh.ttc",
                                          "C:\\Windows\\Fonts\\msyhl.ttc",
                                          "C:\\Windows\\Fonts\\simhei.ttf",
                                          "C:\\Windows\\Fonts\\simsun.ttc",
                                          "C:\\Windows\\Fonts\\Deng.ttf",
                                          "C:\\Windows\\Fonts\\simkai.ttf"};
        ImFont* font = nullptr;
        for (const char* path : kCjkFonts) {
            font = io.Fonts->AddFontFromFileTTF(path, uiFontSize, &uiCfg, kChineseRanges);
            if (font)
                break;
        }
        if (!font)
            font = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\arial.ttf", uiFontSize, &uiCfg, kChineseRanges);
        if (!font)
            io.Fonts->AddFontDefault();

        g_MenuHeadingFont =
            io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\msyhbd.ttc", uiFontSize, &uiCfg, kChineseRanges);

        {
            ImFontConfig iconCfg{};
            iconCfg.FontDataOwnedByAtlas = false;
            iconCfg.OversampleH = 3;
            iconCfg.OversampleV = 3;
            iconCfg.PixelSnapH = false;
            static const ImWchar iconRanges[] = {0x20, 0xFF, 0};
            g_WeaponIconFont = io.Fonts->AddFontFromMemoryTTF(
                undefeated_font::font_data, undefeated_font::font_size, 28.0f, &iconCfg, iconRanges);
        }

        {
            ImFontConfig faCfg{};
            faCfg.FontDataOwnedByAtlas = false;
            faCfg.OversampleH = 3;
            faCfg.OversampleV = 3;
            faCfg.PixelSnapH = false;
            static const ImWchar faRanges[] = {0xF05B, 0xF05B, 0xF06E, 0xF06E, 0xF1DE, 0xF1DE, 0xF07C, 0xF07C, 0};
            g_MenuIconFont = io.Fonts->AddFontFromMemoryTTF(
                MenuIconsFont::font_data, MenuIconsFont::font_size, 16.0f, &faCfg, faRanges);
        }
    }

} // namespace

void MenuTryRebuildFonts() {}

Menu::Menu() {
    activeTab = 1;
    showMenu = false; // must start closed ??" open menu disables relative mouse and drifts the cursor
}

static void
LegitSliderInt(const char* label, int& value, int lo, int hi, const char* format = "%d", bool* unlock = nullptr) {
    ImGui::PushID(label);
    MenuUI::BeginValueRow(label);
    keybind.contextMenu(&value, label, true, float(lo), float(hi));
    const float gear = unlock ? ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x : 0.f;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - gear);
    ImGui::BeginDisabled(unlock && *unlock);
    MenuUI::SliderInt("##slider", &value, lo, hi, format);
    ImGui::EndDisabled();
    keybind.contextMenu(&value, label, true, float(lo), float(hi));
    MenuUI::EndValueRow();
    ImGui::PopID();
    if (unlock) {
        ImGui::SameLine();
        MenuUI::OptionGear(label);
        if (MenuUI::BeginOptionSettings(label)) {
            MenuUI::Checkbox("Unlock", unlock);
            ImGui::EndPopup();
        }
    }
}

static void LegitCheck(const char* label, bool* value) {
    MenuUI::Checkbox(label, value);
}

static void ReadOnlyCheck(const char* label, bool value) {
    ImGui::PushID(label);
    MenuUI::OptionControls(label, ImGui::GetFrameHeight());
    ImGui::BeginDisabled();
    UiText::Checkbox("##forced", &value);
    ImGui::EndDisabled();
    MenuUI::OptionDivider();
    ImGui::PopID();
}

static void LegitDamage(int& value) {
    ImGui::PushID(&value);
    MenuUI::BeginValueRow("Min damage");
    keybind.contextMenu(&value, "Min damage", true, 0, 120);
    ImGui::SetNextItemWidth(-1);
    MenuUI::SliderInt("##damage", &value, 0, 120, MenuUI::DamageLabel(value).c_str(), ImGuiSliderFlags_NoInput);
    keybind.contextMenu(&value, "Min damage", true, 0, 120);
    MenuUI::EndValueRow();
    ImGui::PopID();
}

static void LegitHitchance(Config::LegitbotConfig& c) {
    const char* label = "HC (Hitchance)";
    float shown = c.RequiredHitchance();
    MenuUI::BeginValueRow(label);
    keybind.contextMenu(&c.hitchance, label, true, 0, 100);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() -
                            ImGui::GetStyle().ItemSpacing.x);
    if (UiText::SliderFloat("##hitchance", &shown, c.autostop ? 1.f : 0.f, 100.f, "%.0f%%")) {
        c.hitchance = shown;
        keybind.manualChange(&c.hitchance);
    }
    keybind.contextMenu(&c.hitchance, label, true, 0, 100);
    ImGui::SameLine();
    MenuUI::OptionGear(label);
    MenuUI::EndValueRow();
    if (MenuUI::BeginOptionSettings(label)) {
        static const char* budgets[] = {"64", "128", "256"};
        ui::ComboFull("HC samples", "##hc_budget", &c.hc_budget, budgets, 3);
        ImGui::EndPopup();
    }
}

static void LegitFloat(const char* label, const char* id, float* value, float lo, float hi, const char* format) {
    MenuUI::Slider(label, id, value, lo, hi, format);
}

static void
LegitScaledFloat(const char* label, const char* id, float* value, float lo, float hi, float scale, float offset) {
    ImGui::PushID(id);
    MenuUI::BeginValueRow(label);
    keybind.contextMenu(value, label, true, lo, hi);
    float shown = std::clamp(*value, lo, hi) * scale + offset;
    ImGui::SetNextItemWidth(-1);
    if (UiText::SliderFloat("##slider", &shown, lo * scale + offset, hi * scale + offset, "%.0f")) {
        *value = (shown - offset) / scale;
        keybind.manualChange(value);
    }
    keybind.contextMenu(value, label, true, lo, hi);
    MenuUI::EndValueRow();
    ImGui::PopID();
}

static void LegitFov(const char* label, const char* id, float* value) {
    LegitScaledFloat(label, id, value, 0.f, 90.f, 2.f, 0.f);
}

static void LegitSmooth(const char* label, const char* id, float* value) {
    constexpr float scale = 99.f / 29.f;
    LegitScaledFloat(label, id, value, 1.f, 30.f, scale, 1.f - scale);
}

static void LegitCurveEditor(std::array<float, 8>& points) {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 size(
        (std::max)(120.f, ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x),
        120.f);
    ImGui::InvisibleButton("##custom_curve", size);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(150, 150, 150, 200));
    if (ImGui::IsItemActive()) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const int i = std::clamp(static_cast<int>(std::round((mouse.x - pos.x) / size.x * 7.f)), 0, 7);
        points[i] = std::clamp(1.f - (mouse.y - pos.y) / size.y, 0.f, 1.f);
        keybind.manualChange(&points[i]);
    }
    if (MenuUI::SettingsPopup("##curve_points")) {
        for (int i = 0; i < 8; ++i) {
            ImGui::PushID(i);
            LegitFloat("Curve point", "##point", &points[i], 0.f, 1.f, "%.3f");
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }
    for (int i = 0; i < 8; ++i) {
        const ImVec2 point(pos.x + size.x * i / 7.f, pos.y + size.y * (1.f - points[i]));
        if (i) {
            const ImVec2 prev(pos.x + size.x * (i - 1) / 7.f, pos.y + size.y * (1.f - points[i - 1]));
            draw->AddLine(prev, point, IM_COL32(110, 200, 255, 255), 2.f);
        }
        draw->AddCircleFilled(point, 4.f, IM_COL32(110, 200, 255, 255));
    }
}

static void LegitMenu(float half, float gap) {
    LegitProfiles::RefreshHeld();
    int selected = LegitProfiles::EditIndex();
    const char* names[LegitProfiles::kCount];
    for (int i = 0; i < LegitProfiles::kCount; ++i)
        names[i] = LegitProfiles::Name(i);
    ImGui::SetNextItemWidth(-1.f);
    if (UiText::Combo("##weapon_profile", &selected, names, LegitProfiles::kCount))
        LegitProfiles::SetEditIndex(selected);
    if (selected) {
        bool enabled = LegitProfiles::Override(selected);
        if (UiText::Checkbox("Use this configuration", &enabled))
            LegitProfiles::SetOverride(selected, enabled);
        if (!enabled) {
            const int parent = LegitProfiles::Parent(selected);
            UiText::TextDisabled("Inherits: %s",
                                 Lang::T(LegitProfiles::Name(LegitProfiles::Override(parent) ? parent : 0)));
        }
    }
    auto& c = LegitProfiles::At(LegitProfiles::Override(selected) ? selected : LegitProfiles::Resolve(selected));
    ImGui::BeginDisabled(selected && !LegitProfiles::Override(selected));
    static int page = 0;
    static const char* tabs[] = {"Aim", "Targets", "Humanization", "Trigger / RCS", "Semi-rage"};
    static const char* groups[] = {"Head", "Neck", "Chest", "Stomach", "Pelvis", "Arms", "Legs", "Feet"};
    ui::SubNav(tabs, 5, &page);
    ui::BeginCard("##legit_left", half, true);
    if (page == 0) {
        ui::SectionLabel("Legitbot");
        LegitCheck("Enable Legitbot", &c.enabled);
        LegitDamage(c.aim_min_damage);
        static const char* fallbackModes[] = {"Hold current hitbox", "Pre-aim exposed damage"};
        ui::ComboFull("Below Min damage", "##aim_damage_fallback", &c.aim_damage_fallback, fallbackModes, 2);
        if (MenuUI::OptionSettings("Enable Checks", c.checks)) {
            LegitCheck("Visibility", &c.visible);
            LegitCheck("Air (local)", &c.air);
            LegitCheck("Blinded (local)", &c.blinded);
            LegitCheck("Reload (local)", &c.reload);
            LegitCheck("Switching weapon (local)", &c.switching);
            LegitCheck("Smoke", &c.smoke);
            LegitCheck("Teammate", &c.teammate);
            ImGui::EndPopup();
        }
        ui::SectionLabel("FOV");
        LegitFov("Vertical FOV", "##vertical_fov", &c.vertical_fov);
        LegitFov("Horizontal FOV", "##horizontal_fov", &c.horizontal_fov);
        LegitCheck("Dynamic FOV", &c.dynamic_fov);
    } else if (page == 1) {
        ui::SectionLabel("Targets");
        MenuUI::OptionControls("Hitgroups", ImGui::GetFrameHeight());
        MenuUI::OptionGear("Hitgroups");
        MenuUI::OptionDivider();
        if (MenuUI::BeginOptionSettings("Hitgroups")) {
            for (int i = 0; i < 8; ++i)
                LegitCheck(groups[i], &c.hitgroups[i]);
            ImGui::EndPopup();
        }
        MenuUI::OptionControls("Hitbox priority", ImGui::GetFrameHeight());
        MenuUI::OptionGear("Hitbox priority");
        MenuUI::OptionDivider();
        if (MenuUI::BeginOptionSettings("Hitbox priority")) {
            for (int i = 0; i < 8; ++i) {
                const int group = c.hitbox_order[i];
                if (group < 0 || group >= 8 || !c.hitgroups[group])
                    continue;
                int previous = i - 1, next = i + 1;
                while (previous >= 0 && !c.hitgroups[c.hitbox_order[previous]])
                    --previous;
                while (next < 8 && !c.hitgroups[c.hitbox_order[next]])
                    ++next;
                ImGui::PushID(i);
                UiText::TextUnformatted(groups[group]);
                if (previous >= 0) {
                    ImGui::SameLine();
                    if (UiText::SmallButton("Up")) {
                        std::swap(c.hitbox_order[i], c.hitbox_order[previous]);
                        keybind.manualChange(&c.hitbox_order[i]);
                        keybind.manualChange(&c.hitbox_order[previous]);
                    }
                }
                if (next < 8) {
                    ImGui::SameLine();
                    if (UiText::SmallButton("Down")) {
                        std::swap(c.hitbox_order[i], c.hitbox_order[next]);
                        keybind.manualChange(&c.hitbox_order[i]);
                        keybind.manualChange(&c.hitbox_order[next]);
                    }
                }
                ImGui::PopID();
            }
            ImGui::EndPopup();
        }
        LegitCheck("Aim only visible hitbox", &c.visible_hitbox);
        bool forcedMultipoint = true;
        if (MenuUI::OptionSettings("Multipoint", c.per_hitbox ? forcedMultipoint : c.multipoint, c.per_hitbox)) {
            ImGui::BeginDisabled(c.per_hitbox);
            LegitScaledFloat("Point scale", "##pointscale", &c.point_scale, 0.f, 99.f, 1.f, 0.f);
            ImGui::EndDisabled();
            if (MenuUI::OptionSettings("MP per hitbox", c.per_hitbox)) {
                for (int i = 0; i < 8; ++i)
                    if (c.hitgroups[i]) {
                        ImGui::PushID(i);
                        LegitScaledFloat(groups[i], "##pointscale", &c.point_scales[i], 0.f, 99.f, 1.f, 0.f);
                        ImGui::PopID();
                    }
                ImGui::EndPopup();
            }
            ImGui::EndPopup();
        }
    } else if (page == 2) {
        ui::SectionLabel("Aim curve");
        static const char* curves[] = {"Linear", "SineWave", "Ease", "Bezier", "Custom"};
        ui::ComboFull("Curve", "##curve", &c.curve, curves, 5);
        if (c.curve == 4)
            LegitCurveEditor(c.custom_curve);
        LegitFloat("Curve strength", "##curve_strength", &c.curve_strength, 0.f, 100.f, "%.0f");
        LegitCheck("Random strength", &c.random_strength);
        LegitFloat("Acceleration", "##acceleration", &c.acceleration, 0.f, 100.f, "%.0f");
        LegitFloat("Deceleration", "##deceleration", &c.deceleration, 0.f, 100.f, "%.0f");
        LegitFloat("Speed variation", "##speed_variation", &c.speed_variation, 0.f, 100.f, "%.0f");
        LegitSliderInt("Reaction delay", c.reaction_delay, 0, 500, "%d ms");
    } else if (page == 3) {
        ui::SectionLabel("Triggerbot");
        LegitCheck("Enable Triggerbot", &c.trigger);
        LegitSliderInt("Trigger delay", c.trigger_delay, 0, 100, "%d ms");
        LegitSliderInt("First shot delay", c.first_shot_delay, 0, 100, "%d ms");
        LegitSliderInt("Cooldown", c.cooldown, 0, 500, "%d ms");
        LegitCheck("Burst mode", &c.burst);
        LegitSliderInt("Burst shots", c.burst_shots, 0, 10);
        if (c.autostop || c.seed_trigger) {
            bool off = false;
            ImGui::BeginDisabled();
            ReadOnlyCheck("Velocity check", off);
            ImGui::EndDisabled();
            UiText::TextDisabled(c.seed_trigger ? "Seed Trigger suspends Velocity check and HC."
                                                : "Autostop uses HC; Velocity check is suspended.");
        } else
            LegitCheck("Velocity check", &c.velocity_check_enabled);
        if (c.VelocityCheckEnabled())
            LegitFloat("Speed limit", "##velocity_check", &c.velocity_check, 0.f, 100.f, "%.0f");
        ImGui::BeginDisabled(c.seed_trigger);
        LegitHitchance(c);
        ImGui::EndDisabled();
        LegitDamage(c.min_damage);
        LegitCheck("Scoped only (snipers)", &c.scoped_only);
        ui::SectionLabel("Trigger checks");
        static const char* checks[] = {"Visibility",
                                       "Air (local)",
                                       "Blinded (local)",
                                       "Reload (local)",
                                       "Switching weapon (local)",
                                       "Smoke",
                                       "Teammate"};
        for (int i = 0; i < 7; ++i) {
            ImGui::PushID(i);
            LegitCheck(checks[i], &c.trigger_checks[i]);
            ImGui::PopID();
        }
    }
    if (page == 4) {
        ui::SectionLabel("Semi-rage");
        LegitCheck("Seed Trigger", &c.seed_trigger);
        LegitCheck("Predictive Autostop", &c.autostop);
        if (c.autostop)
            LegitCheck("Air stop", &c.air_autostop);
        LegitCheck("Predictive Autoscope", &c.autoscope);
        UiText::TextWrapped(
            c.seed_trigger ? "Uses predicted spread and Triggerbot Multipoint. Velocity check and HC are suspended."
                           : "Keeps movement at walking speed. Uses Triggerbot damage and HC settings.");
    }
    ui::EndCard();
    if (page == 4) {
        ImGui::EndDisabled();
        return;
    }
    ImGui::SameLine(0, gap);
    ui::BeginCard("##legit_right", 0, true);
    if (page == 0) {
        ui::SectionLabel("Smooth");
        if (MenuUI::OptionSettings("Enable Smoothing", c.smoothing)) {
            static const char* modes[] = {"Assist", "Magnet"};
            ui::ComboFull("Aim mode", "##aim_mode", &c.aim_mode, modes, 2);
            LegitSmooth("Vertical smooth", "##vertical_smooth", &c.vertical_smooth);
            LegitSmooth("Horizontal smooth", "##horizontal_smooth", &c.horizontal_smooth);
            LegitCheck("Dynamic smooth", &c.dynamic_smooth);
            LegitFloat("Aim speed", "##aim_speed", &c.aim_speed, 1.f, 100.f, "%.0f");
            LegitSliderInt("Aim delay", c.aim_delay, 0, 500, "%d ms");
            LegitSliderInt("Aim duration", c.aim_duration, 0, 2000, "%d ms", &c.aim_duration_unlock);
            LegitCheck("Target lock", &c.target_lock);
            LegitFloat("Lock FOV", "##lock_fov", &c.lock_fov, 0.f, 30.f, "%.0f");
            LegitSliderInt("Target switch delay", c.switch_delay, 0, 500, "%d ms");
            LegitSliderInt("Delay after kill", c.kill_delay, 0, 500, "%d ms");
            ImGui::EndPopup();
        }
    } else if (page == 1) {
        ui::SectionLabel("Trigger targets");
        MenuUI::OptionControls("Trigger hitgroups", ImGui::GetFrameHeight());
        MenuUI::OptionGear("Trigger hitgroups");
        MenuUI::OptionDivider();
        if (MenuUI::BeginOptionSettings("Trigger hitgroups")) {
            for (int i = 0; i < 8; ++i) {
                ImGui::PushID(i);
                if (c.TriggerHitgroupRequired(i)) {
                    bool checked = true;
                    ImGui::BeginDisabled();
                    ReadOnlyCheck(groups[i], checked);
                    ImGui::EndDisabled();
                    keybind.contextMenu(&c.trigger_hitgroups[i], groups[i]);
                } else
                    LegitCheck(groups[i], &c.trigger_hitgroups[i]);
                ImGui::PopID();
            }
            ImGui::EndPopup();
        }
        bool forcedMultipoint = true;
        if (MenuUI::OptionSettings("Trigger Multipoint",
                                   c.trigger_per_hitbox ? forcedMultipoint : c.trigger_multipoint,
                                   c.trigger_per_hitbox)) {
            ImGui::BeginDisabled(c.trigger_per_hitbox);
            LegitScaledFloat("Point scale",
                             "##trigger_pointscale",
                             &c.trigger_point_scale,
                             c.TriggerPointMinimum(),
                             100.f,
                             1.f,
                             0.f);
            ImGui::EndDisabled();
            if (MenuUI::OptionSettings("MP per hitbox", c.trigger_per_hitbox)) {
                for (int i = 0; i < 8; ++i)
                    if (c.TriggerHitgroup(i)) {
                        ImGui::PushID(i);
                        LegitScaledFloat(groups[i],
                                         "##trigger_pointscale",
                                         &c.trigger_point_scales[i],
                                         c.TriggerPointMinimum(i),
                                         100.f,
                                         1.f,
                                         0.f);
                        ImGui::PopID();
                    }
                ImGui::EndPopup();
            }
            ImGui::EndPopup();
        }
        ui::SectionLabel("Visuals");
        LegitCheck("Draw aim path", &c.draw_path);
        LegitCheck("Draw aimbot FOV", &c.draw_fov);
        LegitCheck("Draw recoil crosshair", &c.draw_recoil_crosshair);
    } else if (page == 2) {
        ui::SectionLabel("Humanization");
        LegitSliderInt("Overshoot bullet amount", c.overshoot_bullets, 0, 10);
        LegitFloat("Micro correction", "##micro", &c.micro_correction, 0.f, 100.f, "%.0f");
        LegitFloat("Aim drift", "##drift", &c.aim_drift, 0.f, 100.f, "%.0f");
        LegitFloat("Hand tremor", "##tremor", &c.hand_tremor, 0.f, 100.f, "%.0f");
    } else if (page == 3) {
        ui::SectionLabel("RCS");
        if (MenuUI::OptionSettings("Enable RCS", c.rcs)) {
            LegitCheck("Standalone RCS", &c.standalone_rcs);
            LegitFloat("RCS X", "##rcs_x", &c.rcs_x, 0.f, 100.f, "%.0f");
            LegitFloat("RCS Y", "##rcs_y", &c.rcs_y, 0.f, 100.f, "%.0f");
            LegitFloat("RCS smooth", "##rcs_smooth", &c.rcs_smooth, 0.f, 100.f, "%.0f");
            LegitCheck("Dynamic RCS", &c.dynamic_rcs);
            LegitCheck("Reset looking angle after RCS", &c.reset_angle);
            LegitFloat("Reset smoothing", "##reset_smooth", &c.reset_smooth, 0.f, 100.f, "%.0f");
            ImGui::EndPopup();
        }
    }
    ui::EndCard();
    if (page == 1) {
        ui::BeginCard("##legit_target_selection", 0, true);
        ui::SectionLabel("Target selection");
        static const char* priorities[] = {"Crosshair", "Distance", "Health"};
        ui::ComboFull("Target priority", "##target_priority", &c.target_priority, priorities, 3);
        LegitFloat("Target stickiness", "##stickiness", &c.target_stickiness, 0.f, 100.f, "%.0f");
        LegitCheck("Force BAIM", &c.force_baim);
        ui::EndCard();
    }
    ImGui::EndDisabled();
}

void Menu::init(HWND& window,
                ID3D11Device* pDevice,
                ID3D11DeviceContext* pContext,
                ID3D11RenderTargetView* mainRenderTargetView) {
    Lang::Initialize(internal_config::ConfigManager::Folder());
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags = ImGuiConfigFlags_NoMouseCursorChange;
    internal_config::ConfigManager::LoadMenuSize();
    {
        static char s_iniPath[MAX_PATH]{};
        const auto ini = internal_config::ConfigManager::MenuSizePath().parent_path() / "imgui.ini";
        strncpy_s(s_iniPath, ini.string().c_str(), _TRUNCATE);
        io.IniFilename = s_iniPath[0] ? s_iniPath : nullptr;
    }
    // SEH mid-frame (GPU paint-kit seed) can leave BeginChild open.
    // Recover stack without CRT assert popup (Missing EndChild).
    io.ConfigErrorRecovery = true;
    io.ConfigErrorRecoveryEnableAssert = false;
    io.ConfigErrorRecoveryEnableDebugLog = true;
    io.ConfigErrorRecoveryEnableTooltip = false;

    ImGui_ImplWin32_Init(window);
    ImGui_ImplDX11_Init(pDevice, pContext);

    MenuUI::ApplyTheme();
    ImGui::GetIO().FontGlobalScale = 1.f;

    const float uiFontSize = 16.f;
    AddMenuFonts(uiFontSize);
    g_builtFontSize = uiFontSize;
    if (!g_MenuIconFont)
        Con::Error("Menu icon font failed to load");
    else
        Con::Ok("Menu tab icons (Font Awesome solid)");

    std::cout << "initialized menu\n";
}

void Menu::shutdown() noexcept {
    __try {
        if (Config::menu_w >= 640.f && Config::menu_h >= 420.f)
            internal_config::ConfigManager::SaveMenuSize();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    __try {
        if (ImGui::GetCurrentContext()) {
            MenuUI::ClearPageFlip();
            ImGui_ImplDX11_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // best effort - may already be partially torn down
        __try {
            ImGui::DestroyContext(nullptr);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
}

void Menu::render() {
    if (showMenu && !openedOnce) {
        activeTab = std::clamp(Config::menu_default_page, 0, 5);
        openedOnce = true;
    }
    // keybind.pollInputs runs in hkPresent every frame (not only when overlay draws)

    // Instant close: AnimTick(false) zeros state; no draw after toggle off
    MenuUI::AnimTick(showMenu);
    if (!showMenu || !MenuUI::AnimVisible()) {
        // DPI only while menu open ??" keep ESP/HUD at 1x
        if (ImGui::GetIO().FontGlobalScale != 1.f)
            ImGui::GetIO().FontGlobalScale = 1.f;
        return;
    }

    MenuUI::ApplyTheme();
    if (activeTab < 0 || activeTab > 5)
        activeTab = 1;

    MenuUI::Layout L = MenuUI::Layout::Current();
    const float openA = MenuUI::OpenAlpha();

    const float dpi = L.dpi;
    const ImGuiIO& io = ImGui::GetIO();
    // Layout::Current() applies DPI scaling and clamps to the active backbuffer.
    // Keep that size instead of overwriting it with the old fixed 520x500 box.
    const float menuW = L.windowW;
    const float menuH = L.windowH;
    ImGui::SetNextWindowSize(ImVec2(menuW, menuH), ImGuiCond_Always);
    static bool positioned = false;
    if (!positioned) {
        ImGui::SetNextWindowPos(ImVec2((io.DisplaySize.x - menuW) * .5f, (io.DisplaySize.y - menuH) * .5f),
                                ImGuiCond_Always);
        positioned = true;
    }
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(1, 1), io.DisplaySize, MenuUI::DpiResizeConstraint, ImGui::FindWindowByName("kAzeware###kAzeware"));
    ImGui::SetNextWindowBgAlpha(1.f);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, openA);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, L.rounding);

    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * dpi);
    ImGui::Begin("kAzeware###kAzeware", nullptr, flags);
    ImGui::SetWindowFontScale(1.f);

    const ImVec2 wpos = ImGui::GetWindowPos();
    const ImVec2 wsize = ImGui::GetWindowSize();

    // A stale imgui.ini / menu_size.json position can leave the window
    // (partly) off-screen: SetNextWindowPos uses Cond_Once, so the saved
    // position always wins and the on-screen clamp above never applies.
    // The off-screen content column is then invisible while the sidebar
    // edge flickers against the game.  Pull it back any frame it is out of
    // bounds and persist the corrected position.
    {
        const ImVec2 ds = io.DisplaySize;
        bool moved = false;
        ImVec2 p = wpos;
        if (ds.x > 64.f && ds.y > 64.f) {
            if (p.x < 0.f) {
                p.x = 0.f;
                moved = true;
            }
            if (p.y < 0.f) {
                p.y = 0.f;
                moved = true;
            }
            if (p.x + wsize.x > ds.x) {
                p.x = (std::max)(0.f, ds.x - wsize.x);
                moved = true;
            }
            if (p.y + wsize.y > ds.y) {
                p.y = (std::max)(0.f, ds.y - wsize.y);
                moved = true;
            }
        }
        if (moved) {
            ImGui::SetWindowPos(ImVec2(p.x, p.y));
            Config::menu_w = menuW;
            Config::menu_h = menuH;
            Config::menu_x = p.x;
            Config::menu_y = p.y;
            internal_config::ConfigManager::SaveMenuSize();
        }
    }

    const bool dragging = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    if (!dragging && (fabsf(wpos.x - Config::menu_x) > 0.5f || fabsf(wpos.y - Config::menu_y) > 0.5f)) {
        Config::menu_w = wsize.x;
        Config::menu_h = wsize.y;
        Config::menu_x = wpos.x;
        Config::menu_y = wpos.y;
        internal_config::ConfigManager::SaveMenuSize();
    }
    const float round = ImGui::GetStyle().WindowRounding;
    const float pad = L.shellPad;

    ImGui::SetCursorPos(ImVec2(pad, pad));
    MenuUI::BeginHeader(L, wsize.x);
    const float bookmarksH = 35.f * dpi;
    ImGui::SetCursorPos(ImVec2(pad, L.headerH));
    static const char* tabs[] = {"Rage", "Legit", "Visuals", "Skins", "Misc", "Config"};
    const float bookmarkW = (wsize.x - 2 * pad - 5 * 6 * dpi) / 6;
    for (int i = 0; i < 6; i++) {
        if (i)
            ImGui::SameLine(0, 6 * dpi);
        if (MenuUI::NavButton(tabs[i], MenuTabIcon::kAll[i], activeTab == i, ImVec2(bookmarkW, bookmarksH), i) &&
            activeTab != i) {
            MenuUI::NotifyTab(i);
            activeTab = i;
        }
    }
    ImGui::SetCursorPos(ImVec2(pad, L.headerH + bookmarksH));
    const float bodyH = (std::max)(1.f, wsize.y - L.headerH - bookmarksH - pad);
    const float contentW = (std::max)(1.f, wsize.x - 2 * pad);

    MenuUI::BeginContent(L, contentW, bodyH, activeTab);
    {
        static int s_renderTab = -1;
        if (s_renderTab != activeTab) {
            s_renderTab = activeTab;
            ImGui::SetScrollY(0.f);
        }

        const float gap = L.gap;
        float avail = ImGui::GetContentRegionAvail().x;
        if (avail < gap + 2.f)
            avail = gap + 2.f;
        const float half = L.colLeft(avail);

        switch (activeTab) {
        case 0:
            Rage::DrawMenu(half, gap);
            break;
        case 1:
            LegitMenu(half, gap);
            break;

        case 2: // Visuals
        {
            // 0 Players (Local / Friendly / Enemy) | 1 World | 2 Nade Pred
            static int visSub = 0;
            {
                static const char* kVisTabs[] = {"Players", "World", "Nade Pred"};
                ui::SubNav(kVisTabs, 3, &visSub);
            }

            const ImGuiColorEditFlags colFlags =
                ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreview;

            const ImGuiColorEditFlags chamCol = ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar;
            // chams::ChamIds order - visible + ignorez lists
            static const int visIds[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
            static const char* visMats[] = {"Liquid",
                                            "Metallic",
                                            "Matte",
                                            "Flat",
                                            "Bloom",
                                            "Outlines",
                                            "Glow",
                                            "Electric",
                                            "Distortion",
                                            "Hologram",
                                            "Pearl"};
            static const int xqzIds[] = {11, 12, 13, 14, 15, 16, 17, 18};
            static const char* xqzMats[] = {"Liquid IZ",
                                            "Matte IZ",
                                            "Flat IZ",
                                            "Bloom IZ",
                                            "Outlines IZ",
                                            "Glow IZ",
                                            "Distortion IZ",
                                            "Hologram IZ"};
            auto chamCombo =
                [](const char* label, const char* id, int* matId, const int* ids, const char* const* names, int n) {
                    int sel = 0;
                    for (int i = 0; i < n; ++i)
                        if (ids[i] == *matId) {
                            sel = i;
                            break;
                        }
                    MenuUI::BeginValueRow(label);
                    keybind.contextMenu(matId, label, true, float(ids[0]), float(ids[n - 1]));
                    ImGui::SetNextItemWidth(-1);
                    if (UiText::Combo(id, &sel, names, n)) {
                        *matId = ids[sel];
                        keybind.manualChange(matId);
                    }
                    keybind.contextMenu(matId, label, true, float(ids[0]), float(ids[n - 1]));
                    MenuUI::EndValueRow();
                };

            if (visSub == 0) {
                static int playerSub = 2;
                static const char* playerTabs[] = {"Local", "Friendly", "Enemy"};
                ui::SubNav(playerTabs, 3, &playerSub);
                if (playerSub == 0) {
                    const float fit = (std::min)(1.f, ImGui::GetContentRegionAvail().y / (550.f * dpi));
                    ImGui::PushFont(nullptr, ImGui::GetFontSize() * fit);
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5.f * dpi * fit, 2.f * dpi * fit));
                    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.f * dpi * fit, 4.f * dpi * fit));
                    MenuUI::Checkbox("Enable Local", &Config::local_visuals);
                    auto slider =
                        [](const char* name, const char* id, float* value, float lo, float hi, const char* format) {
                            MenuUI::Slider(name, id, value, lo, hi, format);
                            MenuUI::OptionDivider();
                        };
                    auto choice =
                        [](const char* name, const char* id, int* value, const char* const* items, int count) {
                            MenuUI::BeginValueRow(name);
                            keybind.contextMenu(value, name, true, 0, float(count - 1));
                            ImGui::SetNextItemWidth(-1);
                            if (UiText::Combo(id, value, items, count))
                                keybind.manualChange(value);
                            keybind.contextMenu(value, name, true, 0, float(count - 1));
                            MenuUI::EndValueRow();
                        };
                    auto color = [](const char* name, ImVec4& value) {
                        MenuUI::OptionControls(name, ImGui::GetFrameHeight());
                        keybind.contextMenu(&value, name);
                        if (UiText::ColorEdit4((std::string("##color/") + name).c_str(),
                                               &value.x,
                                               ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel |
                                                   ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreview))
                            keybind.manualChange(&value);
                        keybind.contextMenu(&value, name);
                        MenuUI::OptionDivider();
                    };
                    auto material = [&](const char* name, const char* id, int& value) {
                        int selected = 0;
                        for (int i = 0; i < IM_ARRAYSIZE(visIds); ++i)
                            if (visIds[i] == value)
                                selected = i;
                        MenuUI::BeginValueRow(name);
                        keybind.contextMenu(
                            &value, name, true, float(visIds[0]), float(visIds[IM_ARRAYSIZE(visIds) - 1]));
                        ImGui::SetNextItemWidth(-1);
                        if (UiText::Combo(id, &selected, visMats, IM_ARRAYSIZE(visMats))) {
                            value = visIds[selected];
                            keybind.manualChange(&value);
                        }
                        keybind.contextMenu(
                            &value, name, true, float(visIds[0]), float(visIds[IM_ARRAYSIZE(visIds) - 1]));
                        MenuUI::EndValueRow();
                    };
                    auto dimension = [](const char* name, const char* id, float* value, float lo, float hi) {
                        MenuUI::BeginValueRow(name);
                        keybind.contextMenu(value, name);
                        ImGui::SetNextItemWidth(-1);
                        float shown = *value;
                        if (UiText::SliderFloat(id, &shown, lo, hi, "%.2f") && std::isfinite(shown) && shown > 0.f) {
                            *value = shown;
                            keybind.manualChange(value);
                        }
                        keybind.contextMenu(value, name);
                        MenuUI::EndValueRow();
                    };

                    ImGui::BeginGroup();
                    ImGui::BeginDisabled(!Config::local_visuals);
                    MenuUI::BeginCard("##local_models", half, true);
                    MenuUI::Section("Models");
                    if (MenuUI::OptionSettings("Glow", Config::local_glow)) {
                        color("Local Glow Color", Config::local_glow_color);
                        ImGui::EndPopup();
                    }
                    if (MenuUI::OptionSettings("Local Body Chams", Config::localChams)) {
                        material("Local Material", "##local_mat", Config::localChamsMaterial);
                        color("Local Color", Config::colLocalChams);
                        ImGui::EndPopup();
                    }
                    if (MenuUI::OptionSettings("Hand Chams", Config::armChams)) {
                        material("Hand Material", "##hand_mat", Config::armChamsMaterial);
                        color("Hand Color", Config::colArmChams);
                        ImGui::EndPopup();
                    }
                    if (MenuUI::OptionSettings("Gloves Chams", Config::gloveChams)) {
                        material("Gloves Material", "##glove_mat", Config::gloveChamsMaterial);
                        color("Gloves Color", Config::colGloveChams);
                        ImGui::EndPopup();
                    }
                    if (MenuUI::OptionSettings("Weapon Chams", Config::viewmodelChams)) {
                        material("Weapon Material", "##weapon_mat", Config::viewmodelChamsMaterial);
                        color("Weapon Color", Config::colViewmodelChams);
                        ImGui::EndPopup();
                    }
                    MenuUI::EndCard();

                    MenuUI::BeginCard("##local_camera", half, true);
                    MenuUI::Section("Camera");
                    if (MenuUI::OptionSettings("Viewmodel", Config::viewmodel_changer)) {
                        slider("Offset X", "##vm_x", &Config::viewmodel_x, -20.f, 20.f, "%.2f");
                        slider("Offset Y", "##vm_y", &Config::viewmodel_y, -20.f, 20.f, "%.2f");
                        slider("Offset Z", "##vm_z", &Config::viewmodel_z, -20.f, 20.f, "%.2f");
                        slider("Viewmodel FOV", "##vm_fov", &Config::viewmodel_fov, 40.f, 120.f, "%.0f");
                        slider("Yaw", "##vm_yaw", &Config::viewmodel_yaw, -180.f, 180.f, "%.1f deg");
                        slider("Pitch", "##vm_pitch", &Config::viewmodel_pitch, -180.f, 180.f, "%.1f deg");
                        slider("Roll", "##vm_roll", &Config::viewmodel_roll, -180.f, 180.f, "%.1f deg");
                        ImGui::EndPopup();
                    }
                    if (MenuUI::OptionSettings("Third Person", Config::thirdperson)) {
                        slider("Distance", "##tp_dist", &Config::thirdperson_distance, 50.f, 300.f, "%.0f");
                        if (MenuUI::OptionCheckbox("Unlock Range", Config::thirdperson_unlock_range) &&
                            !Config::thirdperson_unlock_range) {
                            Config::thirdperson_horizontal = std::clamp(Config::thirdperson_horizontal, -75.f, 75.f);
                            Config::thirdperson_vertical = std::clamp(Config::thirdperson_vertical, -75.f, 75.f);
                        }
                        if (Config::thirdperson_unlock_range) {
                            MenuUI::BeginValueRow("Left / Right");
                            ImGui::SetNextItemWidth(-1);
                            MenuUI::InputFloat("##tp_horizontal", &Config::thirdperson_horizontal, 0.f, 0.f, "%.1f");
                            MenuUI::EndValueRow();
                            MenuUI::BeginValueRow("Down / Up");
                            ImGui::SetNextItemWidth(-1);
                            MenuUI::InputFloat("##tp_vertical", &Config::thirdperson_vertical, 0.f, 0.f, "%.1f");
                            MenuUI::EndValueRow();
                        } else {
                            slider("Left / Right",
                                   "##tp_horizontal",
                                   &Config::thirdperson_horizontal,
                                   -75.f,
                                   75.f,
                                   "%.0f u");
                            slider("Down / Up", "##tp_vertical", &Config::thirdperson_vertical, -75.f, 75.f, "%.0f u");
                        }
                        ImGui::EndPopup();
                    }
                    if (MenuUI::OptionSettings("Custom FOV", Config::fovEnabled)) {
                        slider("FOV Value", "##world_fov", &Config::fov, 20.f, 160.f, "%.0f");
                        ImGui::EndPopup();
                    }
                    if (MenuUI::OptionSettings("Custom Aspect", Config::aspect_ratio_enabled)) {
                        slider("Ratio", "##aspect_ratio", &Config::aspect_ratio, .5f, 3.5f, "%.3f");
                        const std::pair<const char*, float> ratios[] = {
                            {"4:3", 4.f / 3.f}, {"16:10", 16.f / 10.f}, {"16:9", 16.f / 9.f}, {"21:9", 21.f / 9.f}};
                        for (int i = 0; i < IM_ARRAYSIZE(ratios); ++i) {
                            if (i)
                                ImGui::SameLine();
                            if (UiText::Button(ratios[i].first)) {
                                Config::aspect_ratio = ratios[i].second;
                                keybind.manualChange(&Config::aspect_ratio);
                            }
                        }
                        ImGui::EndPopup();
                    }
                    MenuUI::EndCard();

                    MenuUI::BeginCard("##local_indicators", half, true);
                    MenuUI::Section("Indicators");
                    auto& spread = Config::weapon_spread;
                    if (MenuUI::OptionSettings("Show Spread", spread.enabled)) {
                        MenuUI::OptionCheckbox("Follow Recoil", spread.followRecoil);
                        color("Center Color", spread.center);
                        color("Edge Color", spread.edge);
                        slider("Blend Radius", "##spread_blend", &spread.blendRadius, 1.f, 99.f, "%.0f%%");
                        MenuUI::Tip("Radius where the center and edge colors are mixed equally.");
                        ImGui::EndPopup();
                    }
                    if (MenuUI::OptionSettings("Custom Scope Lines", Config::scope_custom_lines)) {
                        slider("Size", "##scope_size", &Config::scope_line_size, 0.f, 1.f, "%.2f");
                        slider("Gap", "##scope_gap", &Config::scope_line_gap, 0.f, 40.f, "%.0f");
                        slider("Thickness", "##scope_th", &Config::scope_line_thickness, .1f, 6.f, "%.2f");
                        color("Color", Config::scope_line_color);
                        ImGui::EndPopup();
                    }
                    if (MenuUI::OptionSettings("Custom Zoom FOV", Config::scope_zoom_fov)) {
                        slider("Zoom 1", "##scope_fov1", &Config::scope_fov_1, 1.f, 150.f, "%.0f");
                        slider("Zoom 2", "##scope_fov2", &Config::scope_fov_2, 1.f, 150.f, "%.0f");
                        ImGui::EndPopup();
                    }
                    MenuUI::EndCard();
                    ImGui::EndDisabled();
                    ImGui::EndGroup();

                    ImGui::SameLine(0, gap);
                    ImGui::BeginGroup();
                    ImGui::BeginDisabled(!Config::local_visuals);
                    MenuUI::BeginCard("##local_removals", 0, true);
                    MenuUI::Section("Removals");
                    const std::pair<const char*, bool*> removals[] = {
                        {"HUD", &Config::remove_hud},
                        {"Minimap", &Config::remove_radar},
                        {"Local overhead", &Config::remove_local_overhead},
                        {"Friendly overhead", &Config::remove_friendly_overhead},
                        {"Decals", &Config::remove_decals},
                        {"Particle effects", &Config::remove_particles},
                        {"Water effects", &Config::remove_water},
                        {"Firstperson Legs", &Config::remove_legs},
                        {"Hide Viewmodel When Scoped", &Config::scope_hide_viewmodel}};
                    for (const auto& item : removals)
                        MenuUI::OptionCheckbox(item.first, *item.second);
                    MenuUI::EndCard();
                    ImGui::EndDisabled();

                    MenuUI::BeginCard("##local_feedback", 0, true);
                    MenuUI::Section("Bullet Feedback");
                    if (MenuUI::OptionSettings("Bullet Impacts", Config::bullet_impact_effect)) {
                        static const char* types[] = {"Overlay", "Sparks", "Both"};
                        choice("Impact Type",
                               "##impact_type",
                               &Config::bullet_impact_effect_type,
                               types,
                               IM_ARRAYSIZE(types));
                        slider("Impact Duration",
                               "##impact_duration",
                               &Config::bullet_impact_effect_duration,
                               .1f,
                               5.f,
                               "%.1fs");
                        color("Impact Fill", Config::bullet_impact_effect_fill_color);
                        color("Impact Edge", Config::bullet_impact_effect_edge_color);
                        color("Spark Color", Config::bullet_impact_effect_color_spark);
                        if (MenuUI::OptionSettings("Impact Glow", Config::bullet_impact_effect_glow)) {
                            slider("Glow Strength",
                                   "##impact_glow",
                                   &Config::bullet_impact_effect_glow_strength,
                                   0.f,
                                   2.f,
                                   "%.1f");
                            ImGui::EndPopup();
                        }
                        ImGui::EndPopup();
                    }
                    if (MenuUI::OptionSettings("Bullet Tracers", Config::bullet_tracers)) {
                        slider(
                            "Tracer Duration", "##tracer_duration", &Config::bullet_tracer_duration, .1f, 5.f, "%.1fs");
                        slider("Tracer Width", "##tracer_width", &Config::bullet_tracer_thickness, 1.f, 8.f, "%.1f px");
                        slider(
                            "Glow Strength", "##tracer_glow", &Config::bullet_tracer_glow_strength, 0.f, 2.f, "%.1f");
                        color("Tracer Color", Config::bullet_tracer_color);
                        ImGui::EndPopup();
                    }
                    if (MenuUI::OptionSettings("Hitmarker", Config::hitmarker)) {
                        MenuUI::OptionCheckbox("Screen", Config::hitmarker_screen);
                        MenuUI::OptionCheckbox("World 3D", Config::hitmarker_world);
                        MenuUI::OptionCheckbox("Show Damage", Config::hitmarker_show_damage);
                        dimension("Size", "##hm_size", &Config::hitmarker_size, 6.f, 32.f);
                        dimension("Thickness", "##hm_thick", &Config::hitmarker_thickness, 1.f, 5.f);
                        dimension("World Size", "##hm_wsize", &Config::hitmarker_world_size, 4.f, 28.f);
                        slider("Duration", "##hm_dur", &Config::hitmarker_duration, .25f, 2.5f, "%.2f");
                        color("Normal", Config::hitmarker_color);
                        color("Head", Config::hitmarker_head_color);
                        color("Kill", Config::hitmarker_kill_color);
                        ImGui::EndPopup();
                    }
                    if (MenuUI::OptionSettings("Floating Damage", Config::float_damage)) {
                        slider("Float Life", "##fd_life", &Config::float_damage_duration, .3f, 2.5f, "%.2f");
                        slider("Float Speed", "##fd_speed", &Config::float_damage_speed, 20.f, 120.f, "%.0f");
                        color("Dmg", Config::float_damage_color);
                        color("Head", Config::float_damage_head_color);
                        color("Kill", Config::float_damage_kill_color);
                        ImGui::EndPopup();
                    }
                    if (MenuUI::OptionSettings("Rage Shot Log", Config::hitlog)) {
                        slider("Duration", "##hitlog_life", &Config::hitlog_duration, 1.f, 12.f, "%.1f s");
                        MenuUI::SliderInt("Rows", "##hitlog_rows", &Config::hitlog_max_rows, 4, 16);
                        color("Hit colour", Config::hitlog_color);
                        color("Miss colour", Config::hitlog_miss_color);
                        ImGui::EndPopup();
                    }
                    MenuUI::EndCard();
                    ImGui::EndGroup();
                    ImGui::PopStyleVar(2);
                    ImGui::PopFont();
                } else {
                    const bool friendly = playerSub == 1;
                    auto& settings = friendly ? Config::friendly_visuals : Config::enemy_visuals;
                    ImGui::PushID(playerSub);
                    MenuUI::Checkbox(friendly ? "Enable Friendly" : "Enable Enemy", &settings.enabled);
                    ImGui::BeginDisabled(!settings.enabled);
                    ui::BeginCard("##player_left", half, true);
                    ui::SectionLabel("Player ESP");
                    UiText::TextDisabled("Right-click to bind. Use the gear for settings.");

                    if (!friendly) {
                        ui::FeatureToggle(
                            "Visualise Aimbot",
                            &Config::visualise_aimbot,
                            "Preview ragebot shots within 50 units left or right. A charged DT can mark a second target after a lethal first shot.");
                        if (MenuUI::SettingsPopup("##visualise_aimbot", {&Config::visualise_aimbot_color})) {
                            ui::PopupTitle("Visualise Aimbot");
                            MenuUI::ColorEdit4("Color##visualise_aimbot",
                                               (float*)&Config::visualise_aimbot_color,
                                               colFlags | ImGuiColorEditFlags_AlphaBar);
                            ImGui::EndPopup();
                        }
                    }
                    ui::FeatureToggle("Box", &settings.esp, "Draw boxes around players.");
                    if (MenuUI::SettingsPopup("##esp_box_pop", {&settings.espColor, &settings.espColorInvisible})) {
                        ui::PopupTitle("Box Settings");
                        MenuUI::ColorEdit4("Visible##esp_box_vis", (float*)&settings.espColor, colFlags);
                        MenuUI::ColorEdit4("Invisible##esp_box_invis", (float*)&settings.espColorInvisible, colFlags);
                        ui::SliderFull("Thickness", "##esp_th", &settings.espThickness, 1.0f, 5.0f, "%.1f");
                        ui::SliderFull("Width Scale", "##esp_bw", &settings.esp_box_width, 0.28f, 0.70f, "%.2f");
                        const char* styles[] = {"Full", "Corner"};
                        ui::ComboFull("Style", "##esp_style", &settings.esp_box_style, styles, IM_ARRAYSIZE(styles));
                        if (MenuUI::OptionSettings("Fill", settings.espFill)) {
                            ui::SliderFull("Fill Opacity", "##esp_fo", &settings.espFillOpacity, 0.0f, 1.0f, "%.2f");

                            ImGui::EndPopup();
                        }
                        ImGui::EndPopup();
                    }

                    static const char* espPosItems[] = {"Top", "Bottom", "Left", "Right"};

                    ui::FeatureToggle("Health Bar", &settings.showHealth, "Show health bar next to players.");
                    if (MenuUI::SettingsPopup("##esp_hp_pop", {&settings.esp_health_color})) {
                        ui::PopupTitle("Health Bar Settings");
                        MenuUI::Checkbox("Auto Color (HP)", &settings.esp_health_auto);
                        if (!settings.esp_health_auto)
                            MenuUI::ColorEdit4("Color##esp_hp", (float*)&settings.esp_health_color, colFlags);
                        ui::SliderFull("Bar Width", "##esp_hp_bw", &settings.esp_bar_width, 2.0f, 8.0f, "%.0f");
                        ui::ComboFull("Position",
                                      "##esp_pos_health",
                                      &settings.esp_pos_health,
                                      espPosItems,
                                      IM_ARRAYSIZE(espPosItems));
                        ImGui::EndPopup();
                    }

                    ui::FeatureToggle("Armor Bar", &settings.showArmor, "Show armor bar next to players.");
                    if (MenuUI::SettingsPopup("##esp_armor_pop", {&settings.esp_armor_color})) {
                        ui::PopupTitle("Armor Bar Settings");
                        MenuUI::ColorEdit4("Color##esp_armor", (float*)&settings.esp_armor_color, colFlags);
                        ui::SliderFull("Bar Width", "##esp_ar_bw", &settings.esp_bar_width, 2.0f, 8.0f, "%.0f");
                        ui::ComboFull("Position",
                                      "##esp_pos_armor",
                                      &settings.esp_pos_armor,
                                      espPosItems,
                                      IM_ARRAYSIZE(espPosItems));
                        ImGui::EndPopup();
                    }

                    ui::FeatureToggle("Name Tags", &settings.showNameTags, "Show player names.");
                    if (MenuUI::SettingsPopup("##esp_name_pop", {&settings.esp_name_color})) {
                        ui::PopupTitle("Name Settings");
                        MenuUI::ColorEdit4("Color##esp_name", (float*)&settings.esp_name_color, colFlags);
                        MenuUI::Checkbox("Avatar", &settings.esp_name_avatar);
                        ui::ComboFull("Position",
                                      "##esp_pos_name",
                                      &settings.esp_pos_name,
                                      espPosItems,
                                      IM_ARRAYSIZE(espPosItems));
                        ImGui::EndPopup();
                    }

                    ui::FeatureToggle("Weapon ESP", &settings.showWeapon, "Show weapon name on players.");
                    if (MenuUI::SettingsPopup("##esp_wep_pop", {&settings.esp_weapon_color})) {
                        ui::PopupTitle("Weapon Settings");
                        MenuUI::ColorEdit4("Color##esp_wep", (float*)&settings.esp_weapon_color, colFlags);
                        ui::ComboFull("Position",
                                      "##esp_pos_weapon",
                                      &settings.esp_pos_weapon,
                                      espPosItems,
                                      IM_ARRAYSIZE(espPosItems));
                        ImGui::EndPopup();
                    }

                    ui::FeatureToggle("Weapon Icons", &settings.showWeaponIcon, "Show weapon icon on players.");
                    if (MenuUI::SettingsPopup("##esp_wep_icon_pop", {&settings.esp_weapon_icon_color})) {
                        ui::PopupTitle("Weapon Icon Settings");
                        MenuUI::ColorEdit4("Color##esp_wep_icon", (float*)&settings.esp_weapon_icon_color, colFlags);
                        ui::ComboFull("Position",
                                      "##esp_pos_weapon_icon",
                                      &settings.esp_pos_weapon_icon,
                                      espPosItems,
                                      IM_ARRAYSIZE(espPosItems));
                        ImGui::EndPopup();
                    }

                    ui::FeatureToggle("Distance ESP", &settings.showDistance, "Show distance to players.");
                    if (MenuUI::SettingsPopup("##esp_dist_pop", {&settings.esp_distance_color})) {
                        ui::PopupTitle("Distance Settings");
                        MenuUI::ColorEdit4("Color##esp_dist", (float*)&settings.esp_distance_color, colFlags);
                        ui::ComboFull("Position",
                                      "##esp_pos_distance",
                                      &settings.esp_pos_distance,
                                      espPosItems,
                                      IM_ARRAYSIZE(espPosItems));
                        ImGui::EndPopup();
                    }

                    ui::FeatureToggle("Skeleton", &settings.esp_skeleton, "Draw player bones.");
                    if (MenuUI::SettingsPopup("##esp_skel_pop",
                                              {&settings.esp_skeleton_color, &settings.esp_skeleton_color_invisible})) {
                        ui::PopupTitle("Skeleton Settings");
                        MenuUI::Checkbox("Head Circle", &settings.esp_skeleton_head);
                        MenuUI::ColorEdit4("Skel Visible##skel_vis", (float*)&settings.esp_skeleton_color, colFlags);
                        MenuUI::ColorEdit4(
                            "Skel Invisible##skel_invis", (float*)&settings.esp_skeleton_color_invisible, colFlags);
                        ui::SliderFull(
                            "Thickness", "##esp_sk_th", &settings.esp_skeleton_thickness, 1.0f, 4.0f, "%.1f");
                        ImGui::EndPopup();
                    }

                    ui::FeatureToggle("Glow", &settings.glow, "Outline glow on players.");
                    if (MenuUI::SettingsPopup("##esp_glow_pop", {&settings.glow_color, &settings.glow_color_invis})) {
                        ui::PopupTitle("Player Glow Settings");
                        MenuUI::Checkbox("Only Visible", &settings.glow_only_visible);
                        MenuUI::ColorEdit4("Glow Visible##glow_vis", (float*)&settings.glow_color, colFlags);
                        MenuUI::ColorEdit4("Glow Invisible##glow_invis", (float*)&settings.glow_color_invis, colFlags);
                        UiText::TextDisabled("RGB + Alpha own glow only");
                        ImGui::EndPopup();
                    }

                    MenuUI::Checkbox("Visibility Check##esp", &settings.esp_vis_check);

                    ui::EndCard();
                    ImGui::SameLine(0, gap);
                    ui::BeginCard("##player_right", 0, true);
                    ui::SectionLabel("Flags");
                    UiText::TextDisabled("Active state labels (right of box)");
                    MenuUI::Checkbox("Flashed", &settings.flag_flashed);
                    MenuUI::Checkbox("Bomb", &settings.flag_bomb);
                    MenuUI::Checkbox("Scoped", &settings.flag_scoped);
                    MenuUI::Checkbox("Reloading", &settings.flag_reloading);
                    MenuUI::Checkbox("Defusing", &settings.flag_defusing);
                    MenuUI::Checkbox("Money", &settings.flag_money);
                    MenuUI::Checkbox("Kit", &settings.flag_kit);
                    MenuUI::Checkbox("Helmet", &settings.flag_helmet);
                    MenuUI::Checkbox("Nades (H/F/S/M/D)", &settings.flag_nades);

                    ui::SectionLabel("Extra ESP");
                    ui::FeatureToggle("Rank ESP", &settings.esp_rank, "Show competitive rank.");
                    if (MenuUI::SettingsPopup("##esp_rank_pop", {&settings.esp_rank_color})) {
                        ui::PopupTitle("Rank");
                        MenuUI::ColorEdit4("Color##esp_rank", (float*)&settings.esp_rank_color, colFlags);
                        UiText::TextDisabled("Competitive rank label");
                        ImGui::EndPopup();
                    }
                    ui::FeatureToggle("3D Box", &settings.esp_3d_box, "3D box around players.");
                    if (MenuUI::SettingsPopup("##esp_3d_pop", {&settings.esp_3d_box_color})) {
                        ui::PopupTitle("3D Box");
                        MenuUI::ColorEdit4("Color##esp_3d", (float*)&settings.esp_3d_box_color, colFlags);
                        UiText::TextDisabled("3D box around player");
                        ImGui::EndPopup();
                    }
                    if (!friendly) {
                        ui::FeatureToggle("Offscreen Arrows", &settings.esp_oof, "Arrows for enemies off screen.");
                        if (MenuUI::SettingsPopup("##esp_oof_pop", {&settings.esp_oof_color})) {
                            ui::PopupTitle("OOF Arrows");
                            MenuUI::ColorEdit4("Color##esp_oof", (float*)&settings.esp_oof_color, colFlags);
                            UiText::TextDisabled("Distance label | pulse | low-HP pip");
                            ui::SliderFull("Size", "##esp_oof_s", &settings.esp_oof_size, 8.f, 28.f, "%.0f");
                            ui::SliderFull("Radius", "##esp_oof_r", &settings.esp_oof_radius, 80.f, 420.f, "%.0f");

                            ImGui::EndPopup();
                        }
                    }

                    ui::FeatureToggle("Sound ESP", &settings.sound_esp, "Ring when players move nearby.");
                    if (MenuUI::SettingsPopup("##esp_sound_pop", {&settings.sound_esp_color})) {
                        ui::PopupTitle("Sound ESP");
                        ui::SliderFull("Ring Size", "##snd_rs", &settings.sound_esp_ring_size, 0.5f, 3.f, "%.2fx");
                        ui::SliderFull("Life", "##snd_life", &settings.sound_esp_duration, 0.4f, 4.f, "%.1fs");
                        MenuUI::ColorEdit4("Color##sound_esp", (float*)&settings.sound_esp_color, colFlags);
                        UiText::TextDisabled("Ground rings for moving players");
                        ImGui::EndPopup();
                    }
                    if (!friendly) {
                        ui::SectionLabel("Chams");

                        if (MenuUI::OptionSettings("Enemy Chams", Config::enemyChams)) {
                            chamCombo("Visible Material",
                                      "##chams_vis_mat",
                                      &Config::chamsMaterial,
                                      visIds,
                                      visMats,
                                      IM_ARRAYSIZE(visMats));
                            MenuUI::ColorEdit4("Chams Visible##chams_vis", (float*)&Config::colVisualChams, chamCol);

                            ImGui::EndPopup();
                        }

                        if (MenuUI::OptionSettings("Chams XQZ", Config::enemyChamsInvisible)) {
                            chamCombo("XQZ Material",
                                      "##chams_xqz_mat",
                                      &Config::chamsMaterialXQZ,
                                      xqzIds,
                                      xqzMats,
                                      IM_ARRAYSIZE(xqzMats));
                            MenuUI::ColorEdit4("Chams XQZ##chams_xqz", (float*)&Config::colVisualChamsIgnoreZ, chamCol);

                            ImGui::EndPopup();
                        }

                        if (MenuUI::OptionSettings("Hit Chams", Config::enemyHitChams)) {
                            chamCombo("Hit Material",
                                      "##chams_hit_mat",
                                      &Config::hitChamsMaterial,
                                      visIds,
                                      visMats,
                                      IM_ARRAYSIZE(visMats));
                            MenuUI::ColorEdit4("Hit Color", (float*)&Config::hitChamsColor, chamCol);

                            ImGui::EndPopup();
                        }

                        if (MenuUI::OptionSettings("Oldest Backtrack Chams", Config::enemyHistoryChams)) {
                            chamCombo("Oldest Backtrack Material",
                                      "##chams_history_mat",
                                      &Config::historyChamsMaterial,
                                      visIds,
                                      visMats,
                                      IM_ARRAYSIZE(visMats));
                            MenuUI::ColorEdit4("Oldest Backtrack Color", (float*)&Config::historyChamsColor, chamCol);

                            ImGui::EndPopup();
                        }

                        if (MenuUI::OptionSettings("Ragdoll Chams", Config::ragdollChams)) {
                            chamCombo("Ragdoll Material",
                                      "##chams_rag_mat",
                                      &Config::ragdollChamsMaterial,
                                      visIds,
                                      visMats,
                                      IM_ARRAYSIZE(visMats));
                            MenuUI::ColorEdit4("Ragdoll Color", (float*)&Config::colRagdollChams, chamCol);

                            ImGui::EndPopup();
                        }
                    }
                    ui::EndCard();
                    ImGui::EndDisabled();
                    ImGui::PopID();
                }
            } else if (visSub == 1) {
                // ?"??"? World: 2 columns, all cards AutoResizeY (no bottom strip scroll) ?"??"?
                const ImGuiColorEditFlags worldCol = ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar;

                // LEFT column
                ui::BeginCard("##world_left", half, true);
                ui::SectionLabel("Atmosphere");
                UiText::TextDisabled("Map lighting & environment");

                const auto selection = World::Skybox::Selected();
                const auto& skies = World::Skybox::Choices();
                const auto selected = std::find_if(skies.begin(), skies.end(), [&](const auto& sky) {
                    return sky.material == selection;
                });
                const char* currentSky = selected != skies.end() ? selected->label.c_str()
                                         : selection.empty()     ? Lang::T("Map Default")
                                                                 : selection.c_str();
                MenuUI::BeginValueRow("Skybox");
                ImGui::SetNextItemWidth(-1);
                if (UiText::BeginCombo("##skybox", currentSky)) {
                    if (UiText::Selectable("Map Default", selection.empty()))
                        World::Skybox::Select({});
                    for (const auto& sky : skies) {
                        ImGui::PushID(sky.material.c_str());
                        if (ImGui::Selectable(sky.label.c_str(), selection == sky.material))
                            World::Skybox::Select(sky.material);
                        ImGui::PopID();
                    }
                    ImGui::EndCombo();
                }
                MenuUI::EndValueRow();
                if (UiText::Button("Refresh Skyboxes"))
                    World::Skybox::RefreshChoices();
                UiText::TextDisabled("Custom .vmat_c files go in csgo/materials/skybox/custom");
                if (const char* error = World::Skybox::LoadError())
                    UiText::TextDisabled(error);

                if (MenuUI::OptionSettings("Tint Sky", Config::skybox)) {
                    MenuUI::ColorEdit4("Sky Color", (float*)&Config::skybox_color, worldCol);

                    ImGui::EndPopup();
                }
                if (MenuUI::OptionSettings("Tint Clouds", Config::sky_cloud_tint)) {
                    MenuUI::ColorEdit4(
                        "Cloud Color", (float*)&Config::sky_cloud_color, worldCol | ImGuiColorEditFlags_NoAlpha);

                    ImGui::EndPopup();
                }
                if (MenuUI::OptionSettings("Tint Sun", Config::sky_sun_tint)) {
                    MenuUI::ColorEdit4(
                        "Sun Color", (float*)&Config::sky_sun_color, worldCol | ImGuiColorEditFlags_NoAlpha);

                    ImGui::EndPopup();
                }

                if (MenuUI::OptionSettings("Lighting Color", Config::lighting)) {
                    MenuUI::ColorEdit4("Light Color", (float*)&Config::lighting_color, worldCol);

                    ImGui::EndPopup();
                }

                if (MenuUI::OptionSettings("Map Color", Config::map_color)) {
                    MenuUI::ColorEdit4("World Color", (float*)&Config::map_color_value, worldCol);

                    ImGui::EndPopup();
                }
                if (MenuUI::OptionSettings("Props Color", Config::props_color)) {
                    MenuUI::ColorEdit4("Props Color##props_rgba", (float*)&Config::props_color_value, worldCol);

                    ImGui::EndPopup();
                }

                ImGui::Spacing();
                ui::SectionLabel("Items");
                UiText::TextDisabled("Right-click to bind. Use the gear for settings.");

                ui::SectionLabel("Utility Footprints");
                ui::FeatureToggle("Smoke Footprint",
                                  &Config::smoke_range,
                                  "Actual smoke spread projected onto terrain; updates every 16 ticks.");
                if (MenuUI::SettingsPopup("##smoke_range",
                                          {&Config::smoke_range_style.outline, &Config::smoke_range_style.fill})) {
                    MenuUI::ColorEdit4(
                        "Outline Color##smoke_range", (float*)&Config::smoke_range_style.outline, colFlags);
                    MenuUI::ColorEdit4("Fill Color##smoke_range", (float*)&Config::smoke_range_style.fill, colFlags);
                    ui::SliderFull("Outline Thickness",
                                   "##smoke_range_width",
                                   &Config::smoke_range_style.thickness,
                                   .5f,
                                   6.f,
                                   "%.1f px");
                    ImGui::EndPopup();
                }
                ui::FeatureToggle("Fire Footprint",
                                  &Config::fire_range,
                                  "Burning patches projected onto terrain; updates every 16 ticks.");
                if (MenuUI::SettingsPopup("##fire_range",
                                          {&Config::fire_range_style.outline, &Config::fire_range_style.fill})) {
                    MenuUI::ColorEdit4(
                        "Outline Color##fire_range", (float*)&Config::fire_range_style.outline, colFlags);
                    MenuUI::ColorEdit4("Fill Color##fire_range", (float*)&Config::fire_range_style.fill, colFlags);
                    ui::SliderFull("Outline Thickness",
                                   "##fire_range_width",
                                   &Config::fire_range_style.thickness,
                                   .5f,
                                   6.f,
                                   "%.1f px");
                    ImGui::EndPopup();
                }

                ui::FeatureToggle("Dropped Weapons", &Config::world_esp_weapons, "Show guns on the ground.");
                if (MenuUI::SettingsPopup(
                        "##wesp_wep_pop",
                        {&Config::world_esp_weapon_color, &Config::world_esp_weapon_distance_color})) {
                    ui::PopupTitle("Dropped Weapons");
                    MenuUI::ColorEdit4("ESP Color##wesp_wep", (float*)&Config::world_esp_weapon_color, colFlags);
                    MenuUI::Checkbox("Weapon Icon", &Config::world_esp_weapon_icon);
                    if (MenuUI::OptionSettings("Weapon Distance", Config::world_esp_weapon_distance)) {
                        MenuUI::ColorEdit4("Distance Color##wesp_wep_dist",
                                           (float*)&Config::world_esp_weapon_distance_color,
                                           colFlags);

                        ImGui::EndPopup();
                    }
                    ImGui::EndPopup();
                }

                ui::FeatureToggle("Planted Bomb", &Config::world_esp_bomb, "Bomb site, timer, and defuse info.");
                if (MenuUI::SettingsPopup("##wesp_bomb_pop", {&Config::world_esp_bomb_color})) {
                    ui::PopupTitle("Bomb ESP");
                    MenuUI::ColorEdit4("Color##wesp_bomb", (float*)&Config::world_esp_bomb_color, colFlags);
                    MenuUI::Checkbox("Bomb Time", &Config::world_esp_bomb_timer);
                    ImGui::EndPopup();
                }

                ImGui::Spacing();
                ui::SectionLabel("Item Chams");
                UiText::TextDisabled("Dropped weapons and utility");
                if (MenuUI::OptionSettings("Item Chams", Config::itemChams)) {
                    MenuUI::Checkbox("Pistol##chams_item", &Config::itemChamsPistol);
                    MenuUI::Checkbox("SMG##chams_item", &Config::itemChamsSmg);
                    MenuUI::Checkbox("Rifle##chams_item", &Config::itemChamsRifle);
                    MenuUI::Checkbox("Shotgun##chams_item", &Config::itemChamsShotgun);
                    MenuUI::Checkbox("Sniper##chams_item", &Config::itemChamsSniper);
                    MenuUI::Checkbox("Utility##chams_item", &Config::itemChamsUtility);
                    chamCombo("Item Material",
                              "##chams_item_mat",
                              &Config::itemChamsMaterial,
                              visIds,
                              visMats,
                              IM_ARRAYSIZE(visMats));
                    MenuUI::ColorEdit4("Item Color##chams_item", (float*)&Config::colItemChams, chamCol);

                    ImGui::EndPopup();
                }

                if (MenuUI::OptionSettings("Item XQZ", Config::itemChamsInvisible)) {
                    chamCombo("Item XQZ Material",
                              "##chams_item_xqz_mat",
                              &Config::itemChamsMaterialXQZ,
                              xqzIds,
                              xqzMats,
                              IM_ARRAYSIZE(xqzMats));
                    MenuUI::ColorEdit4("Item XQZ Color##chams_item_xqz", (float*)&Config::colItemChamsIgnoreZ, chamCol);

                    ImGui::EndPopup();
                }
                ui::EndCard();

                ImGui::SameLine(0, gap);

                // RIGHT column
                ui::BeginCard("##world_right", 0, true);
                ui::SectionLabel("Weather & Fog");
                UiText::TextDisabled("Particles + gradient fog");

                if (MenuUI::OptionSettings("Weather", Config::weather)) {
                    const char* weatherModes[] = {"Rain", "Snow", "Ash"};
                    constexpr int weatherIds[] = {4, 1, 3};
                    int modeIdx = Config::weather_mode == 4 ? 0 : Config::weather_mode == 3 ? 2 : 1;
                    MenuUI::BeginValueRow("Mode");
                    keybind.contextMenu(&Config::weather_mode, "Mode", true, 1, 4);
                    ImGui::SetNextItemWidth(-1);
                    if (UiText::Combo("##weather_mode", &modeIdx, weatherModes, IM_ARRAYSIZE(weatherModes))) {
                        Config::weather_mode = weatherIds[modeIdx];
                        keybind.manualChange(&Config::weather_mode);
                    }
                    MenuUI::EndValueRow();
                    ui::SliderFull("Intensity", "##weather_int", &Config::weather_intensity, 0.f, 1.f, "%.2f");

                    ImGui::EndPopup();
                }

                ImGui::Spacing();
                if (MenuUI::OptionSettings("Custom Fog", Config::custom_fog)) {
                    MenuUI::ColorEdit4("Fog Color", (float*)&Config::custom_fog_color, worldCol);
                    ui::SliderFull("Fog Start", "##fog_start", &Config::custom_fog_start, 0.f, 4096.f, "%.0f");
                    ui::SliderFull("Fog End", "##fog_end", &Config::custom_fog_end, 0.f, 4096.f, "%.0f");
                    ui::SliderFull("Fog Falloff", "##fog_fall", &Config::custom_fog_falloff, 0.1f, 8.f, "%.2f");

                    ImGui::EndPopup();
                }

                ImGui::Spacing();
                ui::SectionLabel("FX Tint");
                UiText::TextDisabled("Particle / volume colors");

                if (MenuUI::OptionSettings("Smoke Color", Config::smoke_color)) {
                    ImGui::BeginDisabled(Config::remove_smoke);
                    MenuUI::ColorEdit4("Smoke Tint",
                                       (float*)&Config::smoke_color_value,
                                       ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoAlpha);
                    ImGui::EndDisabled();
                    ImGui::EndPopup();
                }

                if (MenuUI::OptionSettings("Fire Color", Config::fire_color)) {
                    MenuUI::ColorEdit4("Fire Tint",
                                       (float*)&Config::fire_color_value,
                                       ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);

                    ImGui::EndPopup();
                }

                if (MenuUI::OptionSettings("Inferno Color", Config::inferno_color)) {
                    MenuUI::ColorEdit4("Inferno Tint",
                                       (float*)&Config::inferno_color_value,
                                       ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);

                    ImGui::EndPopup();
                }

                if (MenuUI::OptionSettings("Explosion Color", Config::explosion_color)) {
                    MenuUI::ColorEdit4("Explosion Tint",
                                       (float*)&Config::explosion_color_value,
                                       ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoAlpha);

                    ImGui::EndPopup();
                }

                ui::SectionLabel("World / FX");
                UiText::TextDisabled("Turn off visual clutter");

                ui::SliderFull("Flash Reduce", "##flash_reduce", &Config::antiflash_amount, 0.f, 100.f, "%.0f%%");

                MenuUI::Checkbox("Smoke", &Config::remove_smoke);

                if (MenuUI::Checkbox("Crosshair", &Config::remove_crosshair)) {
                    if (Config::remove_crosshair)
                        Config::force_crosshair = false;
                }

                if (MenuUI::Checkbox("Force Crosshair", &Config::force_crosshair)) {
                    if (Config::force_crosshair)
                        Config::remove_crosshair = false;
                }

                MenuUI::Checkbox("Visual Recoil", &Config::remove_recoil);

                ui::EndCard();
            } else if (visSub == 2) {
                // Grenade Lineup Helper
                ui::BeginCard("##nade_left", half, true);
                ui::SectionLabel("Grenade Prediction");
                ui::FeatureToggle(
                    "Enable Prediction", &Config::nadepred_enable, "Live trajectory preview while holding a grenade.");
                ImGui::BeginDisabled(!Config::nadepred_enable);
                MenuUI::Checkbox("Show Bounce Dots", &Config::nadepred_show_bounces);
                ImGui::EndDisabled();
                ui::FeatureToggle("All Airborne Trajectories",
                                  &Config::nadepred_in_air,
                                  "Predict thrown grenades from every player, including teammates and enemies.");
                ImGui::BeginDisabled(!Config::nadepred_in_air);
                MenuUI::Checkbox("Utility Name & Distance", &Config::nadepred_air_labels);
                ImGui::EndDisabled();
                MenuUI::ColorEdit4("Trajectory Color", (float*)&Config::nadepred_color, colFlags);
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                ui::SectionLabel("Grenade Helper");
                ui::FeatureToggle(
                    "Enable Grenade Helper", &Config::grenade_helper, "Save and show grenade lineup spots.");
                ImGui::BeginDisabled(!Config::grenade_helper);
                MenuUI::Checkbox("Only Matching Nade", &Config::grenade_helper_only_held);
                ui::SliderFull(
                    "Stand Draw Dist", "##nl_stand", &Config::grenade_helper_stand_dist, 50.f, 1500.f, "%.0f");
                ui::SliderFull("Select Dist", "##nl_select", &Config::grenade_helper_select_dist, 50.f, 2000.f, "%.0f");
                ui::SliderFull("Aim Marker Dist", "##nl_aim", &Config::grenade_helper_aim_dist, 5.f, 80.f, "%.0f");
                MenuUI::ColorEdit4("Stand Color", (float*)&Config::grenade_helper_color, colFlags);
                MenuUI::ColorEdit4("Aim Color", (float*)&Config::grenade_helper_aim_color, colFlags);
                ImGui::EndDisabled();
                ui::EndCard();

                ImGui::SameLine(0, gap);

                ui::BeginCard("##nade_right", 0, true);
                ui::SectionLabel("Capture & Management");
                ImGui::BeginDisabled(!Config::grenade_helper);
                UiText::TextUnformatted("Name");
                ImGui::SetNextItemWidth(-1.f);
                UiText::InputText(
                    "##lineup_name", Config::grenade_helper_capture_name, sizeof(Config::grenade_helper_capture_name));
                // Throw style is auto-detected at capture - show the LIVE style
                // while armed (bind jumpthrows included), "auto" otherwise.
                UiText::TextUnformatted("Throw");
                if (GrenadeHelper::IsCapturing()) {
                    const char* live = GrenadeHelper::ThrowName(GrenadeHelper::CurrentDetected());
                    ImGui::PushStyleColor(ImGuiCol_Text, ui::Accent());
                    UiText::TextUnformatted(live ? live : "Stand");
                    ImGui::PopStyleColor();
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Text, ui::TextMuted());
                    UiText::TextUnformatted("auto (detected at throw)");
                    ImGui::PopStyleColor();
                }
                const char* kindItems[] = {"Any / Held", "HE", "Flash", "Smoke", "Molly", "Decoy"};
                ui::ComboFull("Kind", "##lineup_kind", &Config::grenade_helper_capture_kind, kindItems, 6);
                if (UiText::Button(GrenadeHelper::IsCapturing() ? "Cancel Capture" : "Arm Capture", ImVec2(-1, 0))) {
                    if (GrenadeHelper::IsCapturing())
                        GrenadeHelper::CancelCapture();
                    else {
                        GrenadeHelper::ArmCapture(Config::grenade_helper_capture_name,
                                                  static_cast<GrenadeHelper::NadeKind>(
                                                      std::clamp(Config::grenade_helper_capture_kind, 0, 5)));
                    }
                }
                UiText::TextDisabled("Map: %s  (%d on this map / %d total)",
                                     GrenadeHelper::CurrentMap()[0] ? GrenadeHelper::CurrentMap() : "(none)",
                                     GrenadeHelper::CountCurrentMap(),
                                     static_cast<int>(GrenadeHelper::All().size()));
                UiText::TextDisabled("Save: Documents\\kAzeware\\GrenadeHelpers\\");
                if (!GrenadeHelper::CurrentMap()[0]) {
                    UiText::TextDisabled("Join a map to list its lineups.");
                } else {
                    static int s_renameIdx = -1;
                    static char s_renameBuf[64] = {};
                    static int s_editThrow = 0;

                    if (ImGui::BeginChild("##lineup_list", ImVec2(0, 160), true)) {
                        int shown = 0;
                        auto& lineups = GrenadeHelper::AllMut();
                        for (int i = static_cast<int>(lineups.size()) - 1; i >= 0; --i) {
                            auto& L = lineups[i];
                            if (!GrenadeHelper::IsCurrentMap(L))
                                continue;
                            ImGui::PushID(i);

                            bool en = L.enabled;
                            if (UiText::Checkbox("##en", &en)) {
                                GrenadeHelper::SetEnabledAt(i, en);
                            }
                            ImGui::SameLine();

                            if (s_renameIdx == i) {
                                // Full edit: name + throw type (auto-detect can
                                // mislabel - override by typing/selecting here)
                                ImGui::SetNextItemWidth(-1.f);
                                UiText::InputText(
                                    "##ren", s_renameBuf, sizeof(s_renameBuf), ImGuiInputTextFlags_EnterReturnsTrue);
                                const int throwCount = static_cast<int>(GrenadeHelper::ThrowType::Count);
                                const char* throwItems[15] = {};
                                for (int t = 0; t < throwCount; ++t)
                                    throwItems[t] = GrenadeHelper::ThrowName(static_cast<GrenadeHelper::ThrowType>(t));
                                ImGui::SetNextItemWidth(-1.f);
                                ui::ComboFull("##ren_throw", "##ren_throw_sel", &s_editThrow, throwItems, throwCount);
                                if (UiText::SmallButton("OK")) {
                                    if (s_renameBuf[0])
                                        GrenadeHelper::RenameAt(i, s_renameBuf);
                                    GrenadeHelper::SetThrowAt(i, static_cast<GrenadeHelper::ThrowType>(s_editThrow));
                                    s_renameIdx = -1;
                                }
                                ImGui::SameLine();
                                if (UiText::SmallButton("Cancel")) {
                                    s_renameIdx = -1;
                                }
                            } else {
                                if (!L.enabled)
                                    ImGui::PushStyleColor(ImGuiCol_Text, ui::TextMuted());

                                UiText::Text("%s | %s | %s",
                                             L.name.c_str(),
                                             GrenadeHelper::KindName(L.kind),
                                             GrenadeHelper::ThrowName(L.throwType));

                                if (!L.enabled)
                                    ImGui::PopStyleColor();

                                ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 48.f);
                                if (UiText::SmallButton("Edit")) {
                                    s_renameIdx = i;
                                    strncpy_s(s_renameBuf, sizeof(s_renameBuf), L.name.c_str(), _TRUNCATE);
                                    s_editThrow = static_cast<int>(L.throwType);
                                }
                                ImGui::SameLine();
                                if (UiText::SmallButton("X")) {
                                    if (s_renameIdx == i)
                                        s_renameIdx = -1;
                                    GrenadeHelper::RemoveAt(i);
                                    ImGui::PopID();
                                    break;
                                }
                            }

                            ImGui::PopID();
                            ++shown;
                        }
                        if (shown == 0)
                            UiText::TextDisabled("No lineups for this map.");
                    }
                    ImGui::EndChild();
                }
                if (UiText::Button("Clear This Map"))
                    GrenadeHelper::ClearCurrentMap();
                ImGui::SameLine();
                if (UiText::Button("Reload File"))
                    GrenadeHelper::Load();
                ImGui::EndDisabled();
                ui::EndCard();
            }

        } break;

        case 3: // Skins
        {
            SkinMenu::Draw();
        } break;

        case 4: // Misc
        {
            // Assist - auto pistol
            // Match - auto accept / vote reveal
            ImGui::BeginGroup();
            ui::BeginCard("##misc_left", half, true);
            ui::SectionLabel("Movement");
            for (const auto& feature : Config::MovementFeatures()) {
                ImGui::PushID(feature.id);
                MenuUI::Checkbox(feature.label, feature.enabled);
                const bool details =
                    feature.enabled == &Config::movement.airstrafe || feature.enabled == &Config::movement.quick_peek ||
                    feature.enabled == &Config::movement.edgebug || feature.enabled == &Config::movement.slowwalk;
                if (details && MenuUI::SettingsPopup("##movement_options")) {
                    if (feature.enabled == &Config::movement.airstrafe) {

                        ui::SliderFull("Turning balance",
                                       "##velocity_turn_balance",
                                       &Config::movement.turn_balance,
                                       0.f,
                                       100.f,
                                       "%.0f%%");
                        UiText::TextDisabled("Speed retention <-> Turn response");
                        ui::SliderFull("Turning radius",
                                       "##velocity_turn_radius",
                                       &Config::movement.turn_radius,
                                       0.f,
                                       150.f,
                                       "%.0f u");
                        UiText::TextDisabled("0 = current turning. Limited by server air acceleration.");
                    } else if (feature.enabled == &Config::movement.quick_peek) {

                        MenuUI::Checkbox("On shot", &Config::movement.peek_on_shot);
                        MenuUI::Checkbox("On release", &Config::movement.peek_on_release);
                        MenuUI::Checkbox("Only wait for same-target DT", &Config::movement.peek_dt_same_target);
                        MenuUI::Tip(
                            "Return after the first shot if DT plans to switch targets. Off keeps the existing wait policy.");
                        ui::SliderInt("Arrival braking radius",
                                      "##peek_tolerance",
                                      &Config::movement.peek_tolerance,
                                      2,
                                      16,
                                      "%d u");
                        MenuUI::Checkbox("Stop after returning", &Config::movement.peek_stop);
                        ui::SliderFull(
                            "Marker radius", "##peek_radius", &Config::movement.peek_radius, 4.f, 150.f, "%.0f u");
                        ui::SliderFull("Glow brightness", "##peek_glow", &Config::movement.peek_glow, 0.f, 8.f, "%.2f");
                        MenuUI::ColorEdit4("Marker color",
                                           (float*)&Config::movement.peek_color,
                                           ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
                        MenuUI::Checkbox("Filled marker", &Config::movement.peek_filled);
                    } else if (feature.enabled == &Config::movement.edgebug) {

                        static const char* modes[] = {"Always", "Edge", "No jump held", "Moving", "Fast falling"};
                        ui::ComboFull("Edgebug mode", "##velocity_eb_mode", &Config::movement.edgebug_mode, modes, 5);
                        ui::SliderInt("Duck passes", "##velocity_eb_passes", &Config::movement.edgebug_passes, 1, 5);
                        MenuUI::Checkbox("Include jump steps", &Config::movement.edgebug_jump_steps);
                    } else if (feature.enabled == &Config::movement.slowwalk) {

                        ui::SliderFull("Slowwalk speed",
                                       "##velocity_sw_speed",
                                       &Config::movement.slowwalk_speed,
                                       0.f,
                                       100.f,
                                       "%.0f%%");
                        UiText::TextDisabled(
                            "100%% = maximum accurate ground speed for the current weapon mode. Wheel: +/-10%% while active.");
                    }
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }
            ui::EndCard();
            ui::BeginCard("##misc_match", half, true);
            ui::SectionLabel("Match & utility");
            MenuUI::Checkbox("Straight throw / SuperToss", &Config::grenade_control.straight_throw);
            UiText::TextWrapped(
                "Compensate movement and jump velocity to preserve throw direction. Range can still change.");
            const char* labels[] = {"Auto release HE", "Auto release Flash", "Auto release Fire"};
            for (int i = 0; i < 3; ++i) {
                ImGui::PushID(i);
                auto& release = Config::grenade_control.release[i];
                MenuUI::Checkbox(labels[i], &release.enabled);
                if (MenuUI::SettingsPopup("##release_settings")) {
                    if (i == 0)
                        LegitFloat("Expected damage",
                                   "##grenade_he_damage",
                                   &Config::grenade_control.he_damage,
                                   1.f,
                                   100.f,
                                   "%.0f");
                    MenuUI::Checkbox("Do not affect self", &release.protect_self);
                    MenuUI::Checkbox("Do not affect teammates", &release.protect_team);
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }

            ImGui::Spacing();
            ui::SectionLabel("Match");
            if (MenuUI::OptionSettings("Auto Accept", Config::auto_accept)) {
                ImGui::BeginDisabled(Config::auto_accept_random);
                ui::SliderInt("Accept delay", "##auto_accept_delay", &Config::auto_accept_delay, 0, 19, "%d s");
                ImGui::EndDisabled();
                MenuUI::Checkbox("Random##auto_accept_random", &Config::auto_accept_random);
                if (ImGui::IsItemHovered())
                    UiText::SetTooltip("Choose a new delay from 0 to 19 seconds for each match notification.");

                ImGui::EndPopup();
            }
            MenuUI::Checkbox("Unlock Inventory", &Config::unlock_inventory);
            MenuUI::Checkbox("Vote Reveal", &Config::vote_reveal);
            MenuUI::Checkbox("Scoreboard Weapons", &Config::scoreboard_weapons);
            if (ImGui::IsItemHovered())
                UiText::SetTooltip("Reveal enemy weapons and active weapon icons while holding TAB.");
            ui::EndCard();

            ImGui::EndGroup();
            ImGui::SameLine(0, gap);
            ImGui::BeginGroup();
            ui::BeginCard("##misc_right", 0, true);
            ui::SectionLabel("Melee");
            if (MenuUI::OptionSettings("Knife bot", Config::melee.knife)) {
                MenuUI::Checkbox("Knife Double Tap", &Config::melee.knife_dt);
                static const char* attacks[] = {"Automatic", "Primary only", "Secondary only"};
                ui::ComboFull("Knife attack", "##misc_knife_attack", &Config::melee.knife_attack, attacks, 3);

                ImGui::EndPopup();
            }
            if (MenuUI::OptionSettings("Zeus bot", Config::melee.zeus)) {
                MenuUI::Checkbox("Zeus lethal only", &Config::melee.zeus_lethal);

                ImGui::EndPopup();
            }
            ImGui::Separator();

            if (MenuUI::OptionSettings("Hitsounds", Config::hitsound)) {
                ImGui::PushItemWidth(-1.f);
                const int n = Hitsound::Count();
                // Cache the fixed embedded catalog instead of rebuilding strings per frame
                // for three combos was pure churn while the Misc tab is open.
                static std::vector<std::string> hsNames;     // [0] = "" placeholder
                static std::vector<std::string> hsLabelsDef; // "(none)" + names
                static std::vector<std::string> hsLabelsOpt; // "(same as hit)" + names
                static std::vector<const char*> hsPtrsDef;   // stable c_str() into hsLabelsDef
                static std::vector<const char*> hsPtrsOpt;   // stable c_str() into hsLabelsOpt
                auto buildHsLists = [&]() {
                    hsNames.clear();
                    hsLabelsDef.clear();
                    hsLabelsOpt.clear();
                    hsNames.emplace_back("");
                    hsLabelsDef.emplace_back("(none)");
                    hsLabelsOpt.emplace_back("(same as hit)");
                    for (int i = 0; i < n; ++i) {
                        const char* nm = Hitsound::NameAt(i);
                        hsNames.emplace_back(nm ? nm : "");
                        hsLabelsDef.emplace_back(Hitsound::DisplayNameAt(i));
                        hsLabelsOpt.emplace_back(Hitsound::DisplayNameAt(i));
                    }
                    hsPtrsDef.clear();
                    hsPtrsOpt.clear();
                    for (size_t i = 0; i < hsLabelsDef.size(); ++i) {
                        hsPtrsDef.push_back(hsLabelsDef[i].c_str());
                        hsPtrsOpt.push_back(hsLabelsOpt[i].c_str());
                    }
                };
                static int hsBuiltCount = -1;
                static std::uint64_t hsLanguageRevision = 0;
                if (hsBuiltCount != n || hsLanguageRevision != Lang::Revision()) {
                    hsLanguageRevision = Lang::Revision();
                    buildHsLists();
                    hsBuiltCount = n;
                }
                {
                    UiText::TextUnformatted("Default");
                    auto drawDefault = [&](const char* id, char* buf, int bufSz) {
                        int cur = Hitsound::IndexOf(buf);
                        int sel = (cur < 0) ? 0 : cur + 1;
                        if (sel >= static_cast<int>(hsPtrsDef.size()))
                            sel = 0;
                        if (UiText::Combo(id, &sel, hsPtrsDef.data(), static_cast<int>(hsPtrsDef.size()))) {
                            if (sel == 0)
                                buf[0] = 0;
                            else
                                std::snprintf(buf, bufSz, "%s", hsNames[static_cast<size_t>(sel)].c_str());
                        }
                    };
                    drawDefault("##hs_default", Config::hitsound_file, sizeof(Config::hitsound_file));

                    auto drawOptional = [&](const char* id, char* buf, int bufSz) {
                        int cur = Hitsound::IndexOf(buf);
                        int sel = (cur < 0) ? 0 : cur + 1;
                        if (sel >= static_cast<int>(hsPtrsOpt.size()))
                            sel = 0;
                        if (UiText::Combo(id, &sel, hsPtrsOpt.data(), static_cast<int>(hsPtrsOpt.size()))) {
                            if (sel == 0)
                                buf[0] = 0;
                            else
                                std::snprintf(buf, bufSz, "%s", hsNames[static_cast<size_t>(sel)].c_str());
                        }
                    };
                    UiText::TextUnformatted("Headshot (optional)");
                    drawOptional("##hs_head", Config::hitsound_head, sizeof(Config::hitsound_head));
                    UiText::TextUnformatted("Kill (optional)");
                    drawOptional("##hs_kill", Config::hitsound_kill, sizeof(Config::hitsound_kill));

                    ImGui::PopItemWidth();
                    if (UiText::Button("Test sound", ImVec2(-1.f, 0)))
                        Hitsound::PreviewSelected();
                }
                ImGui::Spacing();
                ImGui::EndPopup();
            }
            ui::EndCard();
            ui::BeginCard("##misc_widgets", 0, true);
            ui::SectionLabel("HUD & Widgets");
            MenuUI::Checkbox("Keybind List", &Config::widget_keybinds);
            if (MenuUI::OptionSettings("Bomb Info", Config::widget_bomb)) {
                MenuUI::Checkbox("Auto Defuse", &Config::auto_defuse);

                ImGui::EndPopup();
            }
            MenuUI::Checkbox("Spectator List", &Config::widget_spectators);
            if (MenuUI::OptionSettings("Free Spectate", Config::enemy_spectate)) {
                MenuUI::Checkbox("3rd Person Cam", &Config::enemy_spectate_thirdperson);

                ImGui::EndPopup();
            }
            MenuUI::Checkbox("Radar Reveal", &Config::radar_reveal);
            ui::EndCard();
            ImGui::EndGroup();
        } break;

        case 5: // Config
        {
            static char configName[128] = "";
            static std::vector<std::string> configList = internal_config::ConfigManager::ListConfigs();
            static int selectedConfigIndex = -1;
            static std::string statusMsg;
            static float statusUntil = 0.f;

            auto setStatus = [&](const char* msg) {
                statusMsg = msg;
                statusUntil = static_cast<float>(ImGui::GetTime()) + 2.5f;
            };

            // Fixed-height cards (not AutoResizeY): Manage + Design content is
            // ~750px tall vs ~455px viewport. AutoResizeY cards grow unbounded
            // (child auto-fit has no height clamp) and the bottom of the card -
            // the font manager / design section - gets cut off at the content
            // edge. Filling the region gives the card an internal scrollbar so
            // nothing clips.
            ImGui::BeginGroup();
            ui::BeginCard("##cfg_left", half, true);
            ui::SectionLabel("Manage");

            ImGui::PushStyleColor(ImGuiCol_Text, ui::TextMuted());
            UiText::TextWrapped("Documents\\kAzeware\\Configs");
            ImGui::PopStyleColor();
            ImGui::Spacing();

            MenuUI::Checkbox("Compress on save", &Config::config_compress);

            ImGui::Spacing();

            ImGui::SetNextItemWidth(-1);
            UiText::InputTextWithHint("##cfgname", "Config name...", configName, IM_ARRAYSIZE(configName));

            ImGui::Spacing();
            const float gapBtn = 6.f;
            {
                const float avail = ImGui::GetContentRegionAvail().x;
                const float base = floorf((std::max)(0.f, avail - gapBtn * 2.f) / 3.f);
                const float last = (std::max)(1.f, avail - base * 2.f - gapBtn * 2.f);

                if (UiText::Button("Save", ImVec2((std::max)(1.f, base), 0))) {
                    if (configName[0] == '\0') {
                        setStatus("Enter a config name");
                    } else if (internal_config::ConfigManager::Save(configName)) {
                        configList = internal_config::ConfigManager::ListConfigs();
                        setStatus("Saved");
                    } else {
                        setStatus("Save failed");
                    }
                }
                ImGui::SameLine(0, gapBtn);
                if (UiText::Button("Load", ImVec2((std::max)(1.f, base), 0))) {
                    if (configName[0] == '\0') {
                        setStatus("Select or type a name");
                    } else if (internal_config::ConfigManager::Load(configName)) {
                        setStatus("Loaded");
                    } else {
                        setStatus("Load failed");
                    }
                }
                ImGui::SameLine(0, gapBtn);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.70f, 0.22f, 0.28f, 0.55f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.28f, 0.35f, 0.70f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.90f, 0.32f, 0.38f, 0.85f));
                if (UiText::Button("Delete", ImVec2(last, 0))) {
                    if (configName[0] == '\0') {
                        setStatus("Select a config first");
                    } else if (internal_config::ConfigManager::Remove(configName)) {
                        configName[0] = '\0';
                        selectedConfigIndex = -1;
                        configList = internal_config::ConfigManager::ListConfigs();
                        setStatus("Deleted");
                    } else {
                        setStatus("Delete failed");
                    }
                }
                ImGui::PopStyleColor(3);
            }

            ImGui::Spacing();
            {
                const float avail = ImGui::GetContentRegionAvail().x;
                const float half0 = floorf((std::max)(0.f, avail - gapBtn) * 0.5f);
                const float half1 = (std::max)(1.f, avail - half0 - gapBtn);
                if (UiText::Button("Open Folder", ImVec2(half0, 0))) {
                    internal_config::ConfigManager::OpenFolder();
                    setStatus("Opened configs folder");
                }
                ImGui::SameLine(0, gapBtn);
                if (UiText::Button("Refresh", ImVec2(half1, 0))) {
                    configList = internal_config::ConfigManager::ListConfigs();
                    setStatus("Refreshed");
                }
            }

            if (!statusMsg.empty() && ImGui::GetTime() < statusUntil) {
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text, ui::Accent());
                UiText::TextUnformatted(statusMsg.c_str());
                ImGui::PopStyleColor();
            }

            ImGui::Spacing();
            ui::EndCard();
            ui::BeginCard("##cfg_design", half, true);
            ui::SectionLabel("Design");

            {
                static const char* items[] = {"100%", "125%", "150%", "200%", "Custom"};
                static const int values[] = {100, 125, 150, 200};
                int selected = 4;
                for (int i = 0; i < 4; i++)
                    if (Config::menu_dpi_scale == values[i])
                        selected = i;
                if (Config::menu_dpi_custom)
                    selected = 4;
                MenuUI::BeginValueRow("DPI Scale");
                keybind.contextMenu(&Config::menu_dpi_scale, "DPI Scale");
                ImGui::SetNextItemWidth(-1);
                if (UiText::Combo("##menu_dpi", &selected, items, IM_ARRAYSIZE(items))) {
                    Config::menu_dpi_custom = selected == 4;
                    if (!Config::menu_dpi_custom) {
                        Config::menu_dpi_scale = values[selected];
                        keybind.manualChange(&Config::menu_dpi_scale);
                    }
                }
                keybind.contextMenu(&Config::menu_dpi_scale, "DPI Scale");
                MenuUI::EndValueRow();
                if (Config::menu_dpi_custom || selected == 4) {
                    MenuUI::InputInt("Custom %", &Config::menu_dpi_scale);
                    if (Config::menu_dpi_scale < 1) {
                        Config::menu_dpi_scale = 1;
                        keybind.manualChange(&Config::menu_dpi_scale);
                    }
                }
            }
            ui::SliderFull("Rounding", "##menu_rounding", &Config::menu_rounding, 0.f, 24.f, "%.0f");
            {
                static const char* pages[] = {"Rage", "Legit", "Visuals", "Skins", "Misc", "Config"};
                ui::ComboFull(
                    "Default page", "##menu_default_page", &Config::menu_default_page, pages, IM_ARRAYSIZE(pages));
                if (ImGui::IsItemHovered())
                    UiText::SetTooltip("Used only when the menu is first opened after injection.");
            }

            ui::EndCard();
            ImGui::EndGroup();

            ImGui::SameLine(0, gap);

            ImGui::BeginGroup();
            ImGui::BeginChild("##cfg_right_column", ImVec2(0, 0));
            const float translationHeight = 270.f * L.dpi;
            ImGui::SetNextWindowSize(ImVec2(0,
                                            (std::max)(120.f * L.dpi,
                                                       ImGui::GetContentRegionAvail().y - translationHeight -
                                                           6.f * L.dpi - 2.f * ImGui::GetStyle().ItemSpacing.y)));
            ui::BeginCard("##cfg_right", 0);
            ui::SectionLabel("Saved Configs");

            if (configList.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, ui::TextMuted());
                UiText::TextUnformatted("No configs yet.");
                UiText::TextUnformatted("Save one to get started.");
                ImGui::PopStyleColor();
            } else {
                const float listH = (std::max)(40.f, ImGui::GetContentRegionAvail().y);
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.f, 0.f, 0.f, 0.15f));
                ImGui::BeginChild("##cfg_list",
                                  ImVec2(-1.f, listH),
                                  ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                                  ImGuiWindowFlags_None);
                for (int i = 0; i < static_cast<int>(configList.size()); ++i) {
                    const bool sel = (selectedConfigIndex == i);
                    if (ImGui::Selectable(configList[i].c_str(), sel)) {
                        selectedConfigIndex = i;
                        strncpy_s(configName, sizeof(configName), configList[i].c_str(), _TRUNCATE);
                    }
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        if (internal_config::ConfigManager::Load(configList[i])) {
                            setStatus("Loaded");
                        } else {
                            setStatus("Load failed");
                        }
                    }
                }
                ImGui::EndChild();
                ImGui::PopStyleColor();
            }
            ui::EndCard();
            MenuUI::Gap();
            ImGui::SetNextWindowSize(ImVec2(0, translationHeight));
            ui::BeginCard("##translations", 0);
            ui::SectionLabel("Translation");
            static std::vector<std::string> translationFiles = Lang::Files();
            static std::string selectedTranslation = Lang::ActiveFile();
            const auto activeTranslation = Lang::ActiveFile();
            UiText::Text("Active: %s", activeTranslation.c_str());
            const float listHeight =
                (std::max)(45.f * L.dpi,
                           ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 2 - 50.f * L.dpi);
            ImGui::BeginChild("##translation_files", ImVec2(0, listHeight), ImGuiChildFlags_Borders);
            for (const auto& filename : translationFiles) {
                // Filenames are user data, not translatable labels.
                if (ImGui::Selectable(filename.c_str(), selectedTranslation == filename))
                    selectedTranslation = filename;
            }
            ImGui::EndChild();
            const float buttonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * .5f;
            ImGui::BeginDisabled(selectedTranslation.empty());
            if (UiText::Button("Apply", ImVec2(buttonWidth, 0)))
                Lang::Apply(selectedTranslation);
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (UiText::Button("Refresh", ImVec2(buttonWidth, 0))) {
                translationFiles = Lang::Files();
            }
            if (UiText::Button("Open Folder", ImVec2(buttonWidth, 0))) {
                const auto folder = Lang::Folder();
                ShellExecuteW(nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(selectedTranslation.empty());
            if (UiText::Button("Open File", ImVec2(buttonWidth, 0))) {
                const auto file = Lang::FilePath(selectedTranslation);
                ShellExecuteW(
                    nullptr, L"open", L"notepad.exe", (L"\"" + file.wstring() + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
            }
            ImGui::EndDisabled();
            const auto translationError = Lang::LastError();
            if (!translationError.empty())
                UiText::TextWrapped("%s", Lang::T(translationError.c_str()));
            ui::EndCard();
            ImGui::EndChild();
            ImGui::EndGroup();
        } break;
        }
    }
    MenuUI::EndContent();

    MenuUI::DrawWindowFrame(ImGui::GetWindowDrawList(), wpos, ImVec2(wpos.x + wsize.x, wpos.y + wsize.y), round);

    MenuUI::DrawPageFlip();
    ImGui::End();
    ImGui::PopFont();
    ImGui::PopStyleVar(2); // window alpha + rounding
}

void Menu::toggleMenu() {
    showMenu = !showMenu;
    if (!showMenu) {
        MenuUI::AnimTick(false);
        if (Config::menu_w >= 360.f && Config::menu_h >= 300.f)
            internal_config::ConfigManager::SaveMenuSize();
    }
}
