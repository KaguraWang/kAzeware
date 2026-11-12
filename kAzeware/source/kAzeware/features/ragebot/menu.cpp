#include "rage.h"
#include "profiles.h"
#include "shots.h"
#include "../legitbot/profiles.h"
#include "../../menu/menu_ui.h"
#include "../../config/translated_ui.h"
#include "../../keybinds/keybinds.h"
#include <cctype>
#include <string_view>

namespace Rage {
    namespace {

        void Number(const char* name, int& value, int lo, int hi, const char* format = "%d", bool settings = false) {
            ImGui::PushID(name);
            MenuUI::BeginValueRow(name);
            keybind.contextMenu(&value, name, true, float(lo), float(hi));
            const float gear = settings ? ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x : 0.f;
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - gear);
            MenuUI::SliderInt("##slider", &value, lo, hi, format);
            keybind.contextMenu(&value, name, true, float(lo), float(hi));
            MenuUI::EndValueRow();
            ImGui::PopID();
            if (settings) {
                ImGui::SameLine();
                MenuUI::OptionGear(name);
            }
        }

        void Damage(Settings& c, int profile) {
            const auto binding = "rage." + std::to_string(profile) + ".damage";
            ImGui::PushID("minimum_damage");
            MenuUI::BeginValueRow("Minimum Damage");
            keybind.contextMenu(binding, "Minimum Damage");
            int value = c.DamageValue();
            ImGui::SetNextItemWidth(-1);
            const bool changed = UiText::SliderInt(
                "##slider", &value, 0, 120, MenuUI::DamageLabel(value, true).c_str(), ImGuiSliderFlags_NoInput);
            if (changed)
                keybind.manualChange(binding, BindValue(std::clamp(value, 0, 120)));
            keybind.contextMenu(binding, "Minimum Damage");
            MenuUI::EndValueRow();
            ImGui::PopID();
        }

        void Scalar(const char* name, float& value, float lo, float hi, const char* format = "%.0f") {
            ImGui::PushID(name);
            MenuUI::BeginValueRow(name);
            keybind.contextMenu(&value, name, true, lo, hi);
            ImGui::SetNextItemWidth(-1);
            MenuUI::SliderFloat("##slider", &value, lo, hi, format);
            keybind.contextMenu(&value, name, true, lo, hi);
            MenuUI::EndValueRow();
            ImGui::PopID();
        }

        template <size_t N> void Choice(const char* name, int& value, const char* const (&items)[N]) {
            ImGui::PushID(name);
            MenuUI::BeginValueRow(name);
            keybind.contextMenu(&value, name, true, 0, float(N - 1));
            ImGui::SetNextItemWidth(-1);
            if (UiText::Combo("##select", &value, items, int(N)))
                keybind.manualChange(&value);
            keybind.contextMenu(&value, name, true, 0, float(N - 1));
            MenuUI::EndValueRow();
            ImGui::PopID();
        }

        int editedProfile = 0;
        char profileSearch[96]{};

        bool WeaponProfile(int index) {
            return index >= LegitProfiles::kGroups || index == 7 || index == 8;
        }

        bool MatchesProfile(int index) {
            const auto contains = [](const char* text) {
                const std::string_view value(text), search(profileSearch);
                return std::search(value.begin(),
                                   value.end(),
                                   search.begin(),
                                   search.end(),
                                   [](unsigned char a, unsigned char b) {
                                       return std::tolower(a) == std::tolower(b);
                                   }) != value.end();
            };
            return profileSearch[0] == '\0' || contains(LegitProfiles::Name(index)) ||
                   contains(Lang::T(LegitProfiles::Name(index)));
        }

        void ConfigPanel(float width) {
            const float dpi = MenuUI::Layout::DpiMul();
            MenuUI::BeginCard("##rage_profiles", width, false);
            MenuUI::Section("Config");
            ImGui::SameLine();
            ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - ImGui::GetFrameHeight());
            if (UiText::Button("+", ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()))) {
                profileSearch[0] = '\0';
                ImGui::OpenPopup("##add_rage_profile");
            }
            ImGui::SetNextWindowSize(ImVec2(360.f * dpi, (std::min)(380.f * dpi, ImGui::GetIO().DisplaySize.y - 24.f)),
                                     ImGuiCond_Appearing);
            if (ImGui::BeginPopup("##add_rage_profile")) {
                UiText::TextUnformatted("Add Accuracy configuration");
                if (ImGui::IsWindowAppearing())
                    ImGui::SetKeyboardFocusHere();
                ImGui::SetNextItemWidth(-1);
                UiText::InputTextWithHint(
                    "##profile_search", "Search weapons / groups", profileSearch, sizeof(profileSearch));
                ImGui::BeginChild("##available_profiles", ImVec2(0, 0));
                const int held = LegitProfiles::HeldWeaponIndex() ? LegitProfiles::HeldWeaponIndex()
                                                                  : LegitProfiles::HeldGroupIndex();
                const auto add = [&](int index) {
                    if (!MatchesProfile(index))
                        return;
                    ImGui::PushID(index);
                    ImGui::BeginDisabled(ProfileOverride(index));
                    if (UiText::Selectable(LegitProfiles::Name(index), false, 0, ImVec2(0, ImGui::GetFrameHeight()))) {
                        SetProfileOverride(index, true);
                        editedProfile = index;
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndDisabled();
                    ImGui::PopID();
                };
                if (held > 0 && MatchesProfile(held)) {
                    UiText::TextDisabled("%s", Lang::T("Held weapon"));
                    add(held);
                    ImGui::Separator();
                }
                for (int kind = 0; kind < 2; ++kind) {
                    bool heading = false;
                    for (int i = 1; i < LegitProfiles::kCount; ++i) {
                        if (i == held || ProfileOverride(i) || WeaponProfile(i) != bool(kind) || !MatchesProfile(i))
                            continue;
                        if (!heading) {
                            UiText::TextDisabled("%s", Lang::T(kind ? "Weapons" : "Weapon groups"));
                            heading = true;
                        }
                        add(i);
                    }
                }
                ImGui::EndChild();
                ImGui::EndPopup();
            }
            ImGui::BeginChild("##configured_profiles", ImVec2(0, 0));
            bool any = false;
            for (int kind = 0; kind < 3; ++kind) {
                bool heading = false;
                for (int i = 0; i < LegitProfiles::kCount; ++i) {
                    const int category = i == 0 ? 0 : WeaponProfile(i) ? 2 : 1;
                    if (category != kind || !ProfileOverride(i))
                        continue;
                    if (!heading && any) {
                        ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(.6f, .6f, .6f, .5f));
                        ImGui::Separator();
                        ImGui::PopStyleColor();
                    }
                    heading = any = true;
                    ImGui::PushID(i);
                    if (UiText::Selectable(
                            LegitProfiles::Name(i), editedProfile == i, 0, ImVec2(0, ImGui::GetFrameHeight())))
                        editedProfile = i;
                    if (i > 0 && ImGui::BeginPopupContextItem("##remove_rage_profile")) {
                        UiText::TextUnformatted("Remove this configuration?");
                        UiText::TextDisabled("%s", Lang::T(LegitProfiles::Name(i)));
                        if (UiText::Button("Remove")) {
                            RemoveProfile(i);
                            if (editedProfile == i)
                                editedProfile = 0;
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::SameLine();
                        if (UiText::Button("Cancel"))
                            ImGui::CloseCurrentPopup();
                        ImGui::EndPopup();
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndChild();
            MenuUI::EndCard();
        }
    } // namespace

    void DrawMenu(float, float gap) {
        std::lock_guard guard(ProfileMutex());
        LegitProfiles::RefreshHeld();
        if (!ProfileOverride(editedProfile))
            editedProfile = 0;
        const float dpi = MenuUI::Layout::DpiMul();
        const float fit = (std::min)(1.f, ImGui::GetContentRegionAvail().y / (660.f * dpi));
        ImGui::PushFont(nullptr, ImGui::GetFontSize() * fit);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5.f * dpi * fit, 2.f * dpi * fit));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.f * dpi * fit, 4.f * dpi * fit));
        const float available = ImGui::GetContentRegionAvail().x;
        const float configWidth = std::clamp(available * .18f, 140.f * dpi, 220.f * dpi);
        const float accuracyWidth = (available - configWidth - 2.f * gap) * .5f;
        const float rightWidth = available - configWidth - accuracyWidth - 2.f * gap;
        ConfigPanel(configWidth);
        const int selected = editedProfile;
        auto& c = Profile(selected);
        auto& g = Profile(0);
        const bool hcFallback = NeedsHitchanceFallback(g);
        ImGui::SameLine(0, gap);
        ImGui::BeginGroup();
        MenuUI::BeginCard("##rage_shooting", accuracyWidth, true);
        MenuUI::Section("Accuracy");
        if (g.body_aim == 2 && !(c.hitboxes[2] || c.hitboxes[3] || c.hitboxes[4]))
            UiText::TextDisabled("%s", Lang::T("Enable at least one body hitbox."));
        MenuUI::OptionControls("Hitboxes", ImGui::GetFrameHeight());
        MenuUI::OptionGear("Hitboxes");
        MenuUI::OptionDivider();
        if (MenuUI::BeginOptionSettings("Hitboxes")) {
            static const char* groups[] = {"Head", "Neck", "Chest", "Stomach", "Pelvis", "Arms", "Legs", "Feet"};
            for (int i = 0; i < 8; ++i)
                MenuUI::OptionCheckbox(groups[i], c.hitboxes[i]);
            ImGui::EndPopup();
        }
        MenuUI::BeginValueRow("Multipoint");
        keybind.contextMenu(&c.point_scale, "Multipoint", true, 0, 100);
        const bool autoMP = c.point_scale == 0.f;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() -
                                ImGui::GetStyle().ItemSpacing.x);
        MenuUI::SliderFloat("##point_scale", &c.point_scale, 0, 100, autoMP ? "Auto" : "%.0f%%");
        ImGui::SameLine();
        MenuUI::OptionGear("Multipoint");
        MenuUI::EndValueRow();
        static const char* autoModes[] = {"Balanced", "Reliability", "Timing"};
        if (MenuUI::BeginOptionSettings("Multipoint")) {
            if (autoMP || c.AutoMultipoint())
                Choice("Auto MP priority", c.auto_multipoint_mode, autoModes);
            MenuUI::OptionCheckbox("MP per hitbox", c.per_hitbox);
            static const char* groups[] = {"Head", "Neck", "Chest", "Stomach", "Pelvis", "Arms", "Legs", "Feet"};
            ImGui::BeginDisabled(!c.per_hitbox);
            for (int i = 0; i < 8; ++i) {
                ImGui::PushID(i);
                ImGui::BeginDisabled(!c.hitboxes[i]);
                Scalar(groups[i], c.point_scales[i], 0, 100, c.point_scales[i] == 0.f ? "Auto" : "%.0f%%");
                ImGui::EndDisabled();
                ImGui::PopID();
            }
            ImGui::EndDisabled();
            ImGui::EndPopup();
        }
        Damage(c, selected);
        ImGui::BeginDisabled((g.no_spread || g.force_no_spread) && !hcFallback);
        Number("Hitchance", c.hitchance, 0, 100, c.hitchance == 0 ? "Auto" : "%d%%", c.hitchance == 0);
        ImGui::EndDisabled();
        if (c.hitchance == 0 && MenuUI::BeginOptionSettings("Hitchance")) {
            Choice("Auto HC priority", c.auto_hitchance_mode, autoModes);
            static const char* budgets[] = {"64", "128", "256"};
            Choice("HC samples", c.hc_budget, budgets);
            ImGui::EndPopup();
        }
        MenuUI::OptionCheckbox("Force Shoot", c.force_shoot);
        MenuUI::Tip("Bypass HC at maximum weapon accuracy. Damage and readiness checks still apply.");
        if (MenuUI::OptionSettings("Delay Shot", c.delay_shot)) {
            Number("Maximum wait", c.delay_ms, 0, 30, "%d ms");
            ImGui::EndPopup();
        }
        static const char* refine[] = {"Off", "Standard"};
        Choice("Refine Shot", c.refine_shot, refine);
        ImGui::Separator();
        MenuUI::OptionControls("Backtrack", ImGui::GetFrameHeight() * 2 + ImGui::GetStyle().ItemSpacing.x);
        MenuUI::OptionGear("Backtrack");
        ImGui::SameLine();
        bool backtrack = c.backtrack != 0;
        if (UiText::Checkbox("##backtrack", &backtrack)) {
            c.backtrack = backtrack ? 1 : 0;
            keybind.manualChange(&c.backtrack);
        }
        keybind.contextMenu(&c.backtrack, "Backtrack", true, 0, 1);
        MenuUI::OptionDivider();
        if (MenuUI::BeginOptionSettings("Backtrack")) {
            int steps = c.history_ms / 5;
            MenuUI::BeginValueRow("Maximum history age");
            keybind.contextMenu(&c.history_ms, "Maximum history age", true, 0, 200);
            ImGui::SetNextItemWidth(-1);
            if (UiText::SliderInt("##age", &steps, 0, 40, (std::to_string(steps * 5) + " ms").c_str())) {
                c.history_ms = steps * 5;
                keybind.manualChange(&c.history_ms);
            }
            keybind.contextMenu(&c.history_ms, "Maximum history age", true, 0, 200);
            MenuUI::EndValueRow();
            ImGui::EndPopup();
        }
        if (MenuUI::OptionSettings("Extrapolation", c.extrapolation)) {
            MenuUI::Tip("The main switch predicts short gaps in enemy updates for shots.");
            Number("Extrapolation amount", c.extrapolation_ticks, 0, 8, "%d ticks");
            MenuUI::Tip(
                "1 tick = 15.625 ms. Predict up to this amount towards the shot time; 0 disables shot extrapolation.");
            MenuUI::OptionCheckbox("Movement extrapolation", c.movement_extrapolation);
            MenuUI::Tip("Independent enemy prediction for Auto Stop, Auto Scope and Visualise Peek.");
            ImGui::EndPopup();
        }
        bool forced = true;
        if (MenuUI::OptionSettings("Auto Scope", hcFallback ? forced : c.auto_scope, hcFallback || g.force_no_spread)) {
            Number("Scope early", c.scope_early, 0, 16, "%d ticks");
            ImGui::EndPopup();
        }
        if (MenuUI::OptionSettings("Auto Stop", hcFallback ? forced : c.auto_stop, hcFallback || g.force_no_spread)) {
            static const char* modes[] = {"Full stop", "Slowwalk"};
            Choice("Stop mode", c.stop_mode, modes);
            Number("Early", c.stop_early, 0, 16, "%d ticks");
            MenuUI::OptionCheckbox("Airstop", c.air_stop);
            ImGui::EndPopup();
        }
        MenuUI::EndCard();
        ImGui::EndGroup();
        ImGui::SameLine(0, gap);
        ImGui::BeginGroup();
        MenuUI::BeginCard("##rage_aim", rightWidth, true);
        MenuUI::Section("Rage Aimbot");
        MenuUI::OptionCheckbox("Enable Ragebot", g.enabled);
        MenuUI::OptionCheckbox("Auto Fire", g.auto_fire);
        MenuUI::OptionCheckbox("Autowall", g.autowall);
        if (MenuUI::OptionSettings("Silent Aim", g.silent)) {
            MenuUI::OptionCheckbox("Hide Shots", g.hide_shots);
            ImGui::EndPopup();
        }
        ImGui::BeginDisabled(selected == 8);
        MenuUI::OptionCheckbox("Double Tap", g.double_tap);
        ImGui::EndDisabled();
        if (selected == 8)
            MenuUI::Tip("Unavailable for R8.");
        MenuUI::OptionCheckbox("Predictive No Spread", g.no_spread);
        MenuUI::OptionCheckbox("Force No Spread", g.force_no_spread);
        MenuUI::Tip("FNS applies to automatic shots. Shotguns compensate the pellet-cluster center.");
        Number("Aim FOV", g.fov, 1, 180, "%d deg");
        MenuUI::Tip("180 degrees includes every direction.");
        static const char* priority[] = {
            "Closest to crosshair", "Highest damage", "Lowest health", "Nearest", "Farthest"};
        Choice("Target priority", g.priority, priority);
        static const char* body[] = {"Off", "Prefer body", "Force body"};
        Choice("Body Aim", g.body_aim, body);
        MenuUI::EndCard();
        MenuUI::BeginCard("##rage_anti_aim", rightWidth, true);
        MenuUI::Section("Anti-Aim");
        MenuUI::OptionCheckbox("Enable Anti-Aim", g.anti_aim);
        ImGui::BeginDisabled(!g.anti_aim);
        {
            static const char* pitchModes[] = {
                "Off", "Up", "Down", "Custom", "Jitter", "Random jitter", "Switch jitter", "Three-way jitter"};
            Choice("Pitch mode", g.pitch_mode, pitchModes);
            ImGui::BeginDisabled(g.pitch_mode != 3);
            Scalar("Pitch", g.pitch, -89, 89, "%.1f deg");
            ImGui::EndDisabled();
            ImGui::BeginDisabled(g.pitch_mode < 4);
            Scalar("Pitch minimum", g.pitch_low, -89, 89, "%.1f deg");
            Scalar("Pitch maximum", g.pitch_high, -89, 89, "%.1f deg");
            ImGui::EndDisabled();
            ImGui::Separator();
            static const char* yawModes[] = {"Off", "Static"};
            Choice("Yaw mode", g.yaw_mode, yawModes);
            static const char* bases[] = {"View", "Rage target", "Closest to crosshair"};
            Choice("Direction basis", g.yaw_basis, bases);
            if (g.yaw_basis)
                MenuUI::Tip("Uses View when no eligible target is available.");
            Scalar("Yaw offset", g.yaw, -180, 180, "%.1f deg");
            MenuUI::OptionCheckbox("Roll correction", g.roll_correction);
            MenuUI::OptionCheckbox("Avoid backstab", g.avoid_backstab);
        }
        ImGui::EndDisabled();
        MenuUI::EndCard();
        ImGui::EndGroup();
        ImGui::PopStyleVar(2);
        ImGui::PopFont();
    }
} // namespace Rage
