#include "../features/ragebot/profiles.h"
#include "../features/legitbot/bindings.h"
#include "../config/translated_ui.h"
#include "keybinds.h"
#include "../config/config.h"
#include "../config/language.h"
#include "../menu/menu_ui.h"
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string_view>
#include "../../../external/imgui/imgui_internal.h"

namespace {
    BindValue Read(const BindingTarget& target) {
        if (target.read)
            return target.read();
        void* p = target.resolve();
        switch (target.kind) {
        case BindKind::Boolean:
            return p ? *static_cast<bool*>(p) : false;
        case BindKind::Integer:
            return p ? *static_cast<int*>(p) : 0;
        case BindKind::Float:
            return p ? *static_cast<float*>(p) : 0.f;
        case BindKind::Color:
            return p ? *static_cast<ImVec4*>(p) : ImVec4{};
        }
        return false;
    }

    bool Equal(const BindValue& a, const BindValue& b) {
        if (a.index() != b.index())
            return false;
        switch (a.index()) {
        case 0:
            return std::get<bool>(a) == std::get<bool>(b);
        case 1:
            return std::get<int>(a) == std::get<int>(b);
        case 2:
            return std::get<float>(a) == std::get<float>(b);
        default: {
            const auto x = std::get<ImVec4>(a), y = std::get<ImVec4>(b);
            return x.x == y.x && x.y == y.y && x.z == y.z && x.w == y.w;
        }
        }
    }

    void Write(BindingTarget& target, const BindValue& value) {
        if (target.write) {
            target.write(value);
            return;
        }
        void* p = target.resolve();
        if (!p)
            return;
        switch (target.kind) {
        case BindKind::Boolean:
            *static_cast<bool*>(p) = std::get<bool>(value);
            break;
        case BindKind::Integer:
            *static_cast<int*>(p) = std::get<int>(value);
            break;
        case BindKind::Float:
            *static_cast<float*>(p) = std::get<float>(value);
            break;
        case BindKind::Color:
            *static_cast<ImVec4*>(p) = std::get<ImVec4>(value);
            break;
        }
    }

    nlohmann::json JsonValue(const BindValue& value) {
        return std::visit(
            [](const auto& v) -> nlohmann::json {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, ImVec4>)
                    return {v.x, v.y, v.z, v.w};
                else
                    return v;
            },
            value);
    }

    BindValue ReadJson(BindKind kind, const nlohmann::json& j) {
        switch (kind) {
        case BindKind::Boolean:
            return j.get<bool>();
        case BindKind::Integer:
            return j.get<int>();
        case BindKind::Float: {
            float v = j.get<float>();
            if (!std::isfinite(v))
                throw std::invalid_argument("Nonfinite binding");
            return v;
        }
        case BindKind::Color: {
            if (!j.is_array() || j.size() != 4)
                throw std::invalid_argument("Invalid RGBA binding");
            ImVec4 v;
            for (int i = 0; i < 4; i++) {
                float c = j.at(i).get<float>();
                if (!std::isfinite(c))
                    throw std::invalid_argument("Nonfinite RGBA binding");
                (&v.x)[i] = std::clamp(c, 0.f, 1.f);
            }
            return v;
        }
        }
        return false;
    }

    bool EditValueControl(const char* label, BindingTarget& target, BindValue& value) {
        switch (target.kind) {
        case BindKind::Boolean:
            return UiText::Checkbox(label, &std::get<bool>(value));
        case BindKind::Integer:
            if (target.id.starts_with("rage.") && target.id.ends_with(".hitchance")) {
                auto& hc = std::get<int>(value);
                return UiText::SliderInt(label, &hc, 0, 100, hc == 0 ? "Auto" : "%d%%");
            }
            if (target.id.starts_with("rage.") &&
                (target.id.ends_with(".auto_hitchance_mode") || target.id.ends_with(".auto_multipoint_mode"))) {
                static const char* policies[] = {"Balanced", "Reliability", "Timing"};
                return UiText::Combo(label, &std::get<int>(value), policies, 3);
            }
            if (target.id.starts_with("rage.") && target.id.ends_with(".damage")) {
                auto& damage = std::get<int>(value);
                const bool changed = UiText::SliderInt(
                    label, &damage, 0, 120, MenuUI::DamageLabel(damage, true).c_str(), ImGuiSliderFlags_NoInput);
                return changed;
            }
            if (target.id.starts_with("legitbot.") &&
                (target.id.ends_with(".min_damage") || target.id.ends_with(".aim_min_damage")))
                return MenuUI::DamageInput(label, &std::get<int>(value));
            if (target.id.ends_with(".hc_budget")) {
                static const char* budgets[] = {"64", "128", "256"};
                return UiText::Combo(label, &std::get<int>(value), budgets, 3);
            }
            if (target.bounded)
                return UiText::SliderInt(
                    label, &std::get<int>(value), static_cast<int>(target.minimum), static_cast<int>(target.maximum));
            return UiText::InputInt(label, &std::get<int>(value));
        case BindKind::Float:
            if (target.id.starts_with("rage.") &&
                (target.id.ends_with(".point_scale") || target.id.find(".point_scales.") != std::string::npos)) {
                auto& mp = std::get<float>(value);
                return UiText::SliderFloat(label, &mp, 0, 100, mp == 0.f ? "Auto" : "%.0f%%");
            }
            if (target.displayScale != 1.f || target.displayOffset != 0.f) {
                float shown = std::get<float>(value) * target.displayScale + target.displayOffset;
                const bool changed =
                    target.bounded ? UiText::SliderFloat(label,
                                                         &shown,
                                                         target.minimum * target.displayScale + target.displayOffset,
                                                         target.maximum * target.displayScale + target.displayOffset)
                                   : UiText::InputFloat(label, &shown);
                if (changed)
                    value = (shown - target.displayOffset) / target.displayScale;
                return changed;
            }
            if (target.bounded)
                return UiText::SliderFloat(label, &std::get<float>(value), target.minimum, target.maximum);
            return UiText::InputFloat(label, &std::get<float>(value));
        case BindKind::Color:
            return UiText::ColorEdit4(
                label, &std::get<ImVec4>(value).x, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoOptions);
        }
        return false;
    }

    bool EditValue(const char* label, BindingTarget& target, BindValue& value) {
        const bool numeric = target.kind == BindKind::Integer || target.kind == BindKind::Float;
        if (!numeric || !label || !label[0] || (label[0] == '#' && label[1] == '#'))
            return EditValueControl(label, target, value);
        ImGui::PushID(label);
        MenuUI::BeginValueRow(label);
        ImGui::SetNextItemWidth(-1);
        const bool changed = EditValueControl("##value", target, value);
        MenuUI::EndValueRow();
        ImGui::PopID();
        return changed;
    }
} // namespace

void Keybinds::registerTarget(const std::string& id,
                              BindKind kind,
                              std::function<void*()> resolve,
                              const std::string& label,
                              std::function<void()> changed) {
    auto [it, inserted] = targets_.try_emplace(id);
    auto& target = it->second;
    target.id = id;
    target.kind = kind;
    target.resolve = std::move(resolve);
    target.changed = std::move(changed);
    if (!label.empty())
        target.label = label;
    else if (inserted)
        target.label = id;
    if (inserted)
        target.base = target.applied = Read(target);
}

void Keybinds::initialize() {
    if (initialized_)
        return;
    initialized_ = true;
    LegitBinds::Register(*this);
    Rage::RegisterBindings(*this);
    registerValue("local_visuals", Config::local_visuals);
    registerValue("local_glow", Config::local_glow);
    registerValue("local_glow_color", Config::local_glow_color);
    registerValue("weapon_spread", Config::weapon_spread.enabled);
    registerValue("weapon_spread_follow_recoil", Config::weapon_spread.followRecoil);
    registerValue("weapon_spread_blend_radius", Config::weapon_spread.blendRadius);
    registerValue("weapon_spread_center", Config::weapon_spread.center);
    registerValue("weapon_spread_edge", Config::weapon_spread.edge);
    registerValue("config_compress", Config::config_compress);
    registerValue("glow_world_weapons", Config::glow_world_weapons);
    registerValue("glow_world_weapon_color", Config::glow_world_weapon_color);
    registerValue("glow_world_grenades", Config::glow_world_grenades);
    registerValue("float_damage", Config::float_damage);
    registerValue("float_damage_duration", Config::float_damage_duration);
    registerValue("float_damage_speed", Config::float_damage_speed);
    registerValue("float_damage_color", Config::float_damage_color);
    registerValue("float_damage_head_color", Config::float_damage_head_color);
    registerValue("float_damage_kill_color", Config::float_damage_kill_color);
    registerValue("world_esp_weapons", Config::world_esp_weapons);
    registerValue("world_esp_weapon_icon", Config::world_esp_weapon_icon);
    registerValue("world_esp_weapon_distance", Config::world_esp_weapon_distance);
    registerValue("world_esp_bomb", Config::world_esp_bomb);
    registerValue("world_esp_bomb_timer", Config::world_esp_bomb_timer);
    registerValue("smoke_range", Config::smoke_range);
    registerValue("smoke_range_outline", Config::smoke_range_style.outline);
    registerValue("smoke_range_fill", Config::smoke_range_style.fill);
    registerValue("smoke_range_thickness", Config::smoke_range_style.thickness);
    registerValue("fire_range", Config::fire_range);
    registerValue("fire_range_outline", Config::fire_range_style.outline);
    registerValue("fire_range_fill", Config::fire_range_style.fill);
    registerValue("fire_range_thickness", Config::fire_range_style.thickness);
    registerValue("world_esp_smoke", Config::world_esp_smoke);
    registerValue("world_esp_molotov", Config::world_esp_molotov);
    registerValue("world_esp_he", Config::world_esp_he);
    registerValue("world_esp_flash", Config::world_esp_flash);
    registerValue("world_esp_decoy", Config::world_esp_decoy);
    registerValue("world_esp_weapon_color", Config::world_esp_weapon_color);
    registerValue("world_esp_weapon_distance_color", Config::world_esp_weapon_distance_color);
    registerValue("world_esp_bomb_color", Config::world_esp_bomb_color);
    registerValue("world_esp_smoke_color", Config::world_esp_smoke_color);
    registerValue("world_esp_molotov_color", Config::world_esp_molotov_color);
    registerValue("world_esp_he_color", Config::world_esp_he_color);
    registerValue("world_esp_flash_color", Config::world_esp_flash_color);
    registerValue("world_esp_decoy_color", Config::world_esp_decoy_color);
    registerValue("skybox", Config::skybox);
    registerValue("skybox_color", Config::skybox_color);
    registerValue("sky_cloud_tint", Config::sky_cloud_tint);
    registerValue("sky_cloud_color", Config::sky_cloud_color);
    registerValue("sky_sun_tint", Config::sky_sun_tint);
    registerValue("sky_sun_color", Config::sky_sun_color);
    registerValue("lighting", Config::lighting);
    registerValue("lighting_color", Config::lighting_color);
    registerValue("map_color", Config::map_color);
    registerValue("map_color_value", Config::map_color_value);
    registerValue("props_color", Config::props_color);
    registerValue("props_color_value", Config::props_color_value);
    registerValue("custom_fog", Config::custom_fog);
    registerValue("custom_fog_color", Config::custom_fog_color);
    registerValue("custom_fog_start", Config::custom_fog_start);
    registerValue("custom_fog_end", Config::custom_fog_end);
    registerValue("custom_fog_falloff", Config::custom_fog_falloff);
    registerValue("weather", Config::weather);
    registerValue("weather_mode", Config::weather_mode);
    registerValue("weather_intensity", Config::weather_intensity);
    registerValue("enemyChams", Config::enemyChams);
    registerValue("enemyChamsInvisible", Config::enemyChamsInvisible);
    registerValue("chamsMaterial", Config::chamsMaterial);
    registerValue("chamsMaterialXQZ", Config::chamsMaterialXQZ);
    registerValue("colVisualChams", Config::colVisualChams);
    registerValue("colVisualChamsIgnoreZ", Config::colVisualChamsIgnoreZ);
    registerValue("enemyHitChams", Config::enemyHitChams);
    registerValue("enemyHistoryChams", Config::enemyHistoryChams);
    registerValue("hitChamsMaterial", Config::hitChamsMaterial);
    registerValue("historyChamsMaterial", Config::historyChamsMaterial);
    registerValue("hitChamsColor", Config::hitChamsColor);
    registerValue("historyChamsColor", Config::historyChamsColor);
    registerValue("teamChams", Config::teamChams);
    registerValue("teamChamsInvisible", Config::teamChamsInvisible);
    registerValue("teamChamsMaterial", Config::teamChamsMaterial);
    registerValue("teamChamsMaterialXQZ", Config::teamChamsMaterialXQZ);
    registerValue("teamcolVisualChams", Config::teamcolVisualChams);
    registerValue("teamcolVisualChamsIgnoreZ", Config::teamcolVisualChamsIgnoreZ);
    registerValue("localChams", Config::localChams);
    registerValue("localChamsMaterial", Config::localChamsMaterial);
    registerValue("colLocalChams", Config::colLocalChams);
    registerValue("ragdollChams", Config::ragdollChams);
    registerValue("ragdollChamsMaterial", Config::ragdollChamsMaterial);
    registerValue("colRagdollChams", Config::colRagdollChams);
    registerValue("armChams", Config::armChams);
    registerValue("armChamsMaterial", Config::armChamsMaterial);
    registerValue("colArmChams", Config::colArmChams);
    registerValue("gloveChams", Config::gloveChams);
    registerValue("gloveChamsMaterial", Config::gloveChamsMaterial);
    registerValue("colGloveChams", Config::colGloveChams);
    registerValue("viewmodelChams", Config::viewmodelChams);
    registerValue("viewmodelChamsMaterial", Config::viewmodelChamsMaterial);
    registerValue("colViewmodelChams", Config::colViewmodelChams);
    registerValue("itemChams", Config::itemChams);
    registerValue("itemChamsInvisible", Config::itemChamsInvisible);
    registerValue("itemChamsMaterial", Config::itemChamsMaterial);
    registerValue("itemChamsMaterialXQZ", Config::itemChamsMaterialXQZ);
    registerValue("colItemChams", Config::colItemChams);
    registerValue("colItemChamsIgnoreZ", Config::colItemChamsIgnoreZ);
    registerValue("itemChamsPistol", Config::itemChamsPistol);
    registerValue("itemChamsSmg", Config::itemChamsSmg);
    registerValue("itemChamsRifle", Config::itemChamsRifle);
    registerValue("itemChamsShotgun", Config::itemChamsShotgun);
    registerValue("itemChamsSniper", Config::itemChamsSniper);
    registerValue("itemChamsUtility", Config::itemChamsUtility);
    registerValue("fovEnabled", Config::fovEnabled);
    registerValue("aspect_ratio_enabled", Config::aspect_ratio_enabled);
    registerValue("aspect_ratio", Config::aspect_ratio);
    registerValue("fov", Config::fov);
    registerValue("viewmodel_changer", Config::viewmodel_changer);
    registerValue("viewmodel_fov", Config::viewmodel_fov);
    registerValue("viewmodel_x", Config::viewmodel_x);
    registerValue("viewmodel_y", Config::viewmodel_y);
    registerValue("viewmodel_z", Config::viewmodel_z);
    registerValue("viewmodel_yaw", Config::viewmodel_yaw);
    registerValue("viewmodel_pitch", Config::viewmodel_pitch);
    registerValue("viewmodel_roll", Config::viewmodel_roll);
    registerValue("thirdperson", Config::thirdperson);
    registerValue("thirdperson_distance", Config::thirdperson_distance);
    registerValue("thirdperson_horizontal", Config::thirdperson_horizontal);
    registerValue("thirdperson_vertical", Config::thirdperson_vertical);
    registerValue("thirdperson_unlock_range", Config::thirdperson_unlock_range);
    registerValue("thirdperson_key", Config::thirdperson_key);
    registerValue("thirdperson_key_mode", Config::thirdperson_key_mode);
    registerValue("antiflash_amount", Config::antiflash_amount);
    registerValue("remove_legs", Config::remove_legs);
    registerValue("remove_smoke", Config::remove_smoke);
    registerValue("remove_decals", Config::remove_decals);
    registerValue("smoke_color", Config::smoke_color);
    registerValue("smoke_color_value", Config::smoke_color_value);
    registerValue("fire_color", Config::fire_color);
    registerValue("fire_color_value", Config::fire_color_value);
    registerValue("inferno_color", Config::inferno_color);
    registerValue("inferno_color_value", Config::inferno_color_value);
    registerValue("explosion_color", Config::explosion_color);
    registerValue("explosion_color_value", Config::explosion_color_value);
    registerValue("remove_crosshair", Config::remove_crosshair);
    registerValue("force_crosshair", Config::force_crosshair);
    registerValue("remove_recoil", Config::remove_recoil);
    registerValue("enemy_spectate", Config::enemy_spectate);
    registerValue("enemy_spectate_thirdperson", Config::enemy_spectate_thirdperson);
    registerValue("auto_defuse", Config::auto_defuse);
    registerValue("scope_custom_lines", Config::scope_custom_lines);
    registerValue("scope_line_size", Config::scope_line_size);
    registerValue("scope_line_gap", Config::scope_line_gap);
    registerValue("scope_line_thickness", Config::scope_line_thickness);
    registerValue("scope_line_color", Config::scope_line_color);
    registerValue("scope_zoom_fov", Config::scope_zoom_fov);
    registerValue("scope_fov_1", Config::scope_fov_1);
    registerValue("scope_fov_2", Config::scope_fov_2);
    registerValue("scope_hide_viewmodel", Config::scope_hide_viewmodel);
    registerValue("visualise_aimbot", Config::visualise_aimbot);
    registerValue("visualise_aimbot_color", Config::visualise_aimbot_color);
    registerValue("hitlog", Config::hitlog);
    registerValue("hitlog_duration", Config::hitlog_duration);
    registerValue("hitlog_max_rows", Config::hitlog_max_rows);
    registerValue("hitlog_color", Config::hitlog_color);
    registerValue("hitlog_miss_color", Config::hitlog_miss_color);
    registerValue("bullet_impact_effect", Config::bullet_impact_effect);
    registerValue("bullet_impact_effect_type", Config::bullet_impact_effect_type);
    registerValue("bullet_impact_effect_fill_color", Config::bullet_impact_effect_fill_color);
    registerValue("bullet_impact_effect_edge_color", Config::bullet_impact_effect_edge_color);
    registerValue("bullet_impact_effect_color_spark", Config::bullet_impact_effect_color_spark);
    registerValue("bullet_impact_effect_duration", Config::bullet_impact_effect_duration);
    registerValue("bullet_impact_effect_glow", Config::bullet_impact_effect_glow);
    registerValue("bullet_impact_effect_glow_strength", Config::bullet_impact_effect_glow_strength);
    registerValue("bullet_tracers", Config::bullet_tracers);
    registerValue("bullet_tracer_color", Config::bullet_tracer_color);
    registerValue("bullet_tracer_duration", Config::bullet_tracer_duration);
    registerValue("bullet_tracer_thickness", Config::bullet_tracer_thickness);
    registerValue("bullet_tracer_glow_strength", Config::bullet_tracer_glow_strength);
    registerValue("auto_accept", Config::auto_accept);
    registerValue("auto_accept_delay", Config::auto_accept_delay);
    registerValue("auto_accept_random", Config::auto_accept_random);
    registerValue("scoreboard_weapons", Config::scoreboard_weapons);
    registerValue("unlock_inventory", Config::unlock_inventory);
    registerValue("vote_reveal", Config::vote_reveal);
    registerValue("vote_auto", Config::vote_auto);
    registerValue("vote_auto_choice", Config::vote_auto_choice);
    registerValue("vote_auto_delay_ms", Config::vote_auto_delay_ms);
    registerValue("hitmarker", Config::hitmarker);
    registerValue("hitmarker_screen", Config::hitmarker_screen);
    registerValue("hitmarker_world", Config::hitmarker_world);
    registerValue("hitmarker_show_damage", Config::hitmarker_show_damage);
    registerValue("hitmarker_size", Config::hitmarker_size);
    registerValue("hitmarker_thickness", Config::hitmarker_thickness);
    registerValue("hitmarker_world_size", Config::hitmarker_world_size);
    registerValue("hitmarker_duration", Config::hitmarker_duration);
    registerValue("hitmarker_color", Config::hitmarker_color);
    registerValue("hitmarker_head_color", Config::hitmarker_head_color);
    registerValue("hitmarker_kill_color", Config::hitmarker_kill_color);
    registerValue("hitsound", Config::hitsound);
    registerValue("widget_keybinds", Config::widget_keybinds);
    registerValue("widget_bomb", Config::widget_bomb);
    registerValue("widget_spectators", Config::widget_spectators);
    registerValue("remove_hud", Config::remove_hud);
    registerValue("remove_radar", Config::remove_radar);
    registerValue("remove_local_overhead", Config::remove_local_overhead);
    registerValue("remove_friendly_overhead", Config::remove_friendly_overhead);
    registerValue("remove_particles", Config::remove_particles);
    registerValue("remove_water", Config::remove_water);
    registerValue("radar_reveal", Config::radar_reveal);
    registerValue("widget_keybinds_only_when_active", Config::widget_keybinds_only_when_active);
    registerValue("widget_keybinds_show_all", Config::widget_keybinds_show_all);
    registerValue("menu_rounding", Config::menu_rounding);
    registerValue("menu_dpi_scale", Config::menu_dpi_scale);
    registerValue("menu_default_page", Config::menu_default_page);
    registerValue("widget_keybinds_accent", Config::widget_keybinds_accent);
    registerValue("widget_bomb_urgent", Config::widget_bomb_urgent);
    registerValue("widget_bomb_show_damage", Config::widget_bomb_show_damage);
    registerValue("widget_bomb_show_defuse", Config::widget_bomb_show_defuse);
    registerValue("widget_spectators_accent", Config::widget_spectators_accent);
    registerValue("widget_spectators_show_avatars", Config::widget_spectators_show_avatars);
    registerValue("widget_spectators_max", Config::widget_spectators_max);
    registerValue("nadepred_enable", Config::nadepred_enable);
    registerValue("grenade.straight_throw", Config::grenade_control.straight_throw);
    registerValue("grenade.he_damage", Config::grenade_control.he_damage);
    for (int i = 0; i < 3; ++i) {
        auto& c = Config::grenade_control.release[i];
        const auto id = "grenade.release." + std::to_string(i) + ".";
        registerValue(id + "enabled", c.enabled);
        registerValue(id + "protect_self", c.protect_self);
        registerValue(id + "protect_team", c.protect_team);
    }
    registerValue("nadepred_show_bounces", Config::nadepred_show_bounces);
    registerValue("nadepred_in_air", Config::nadepred_in_air);
    registerValue("nadepred_air_labels", Config::nadepred_air_labels);
    registerValue("nadepred_color", Config::nadepred_color);
    registerValue("enemy_visuals.enabled", Config::enemy_visuals.enabled);
    registerValue("enemy_visuals.esp", Config::enemy_visuals.esp);
    registerValue("enemy_visuals.showHealth", Config::enemy_visuals.showHealth);
    registerValue("enemy_visuals.showArmor", Config::enemy_visuals.showArmor);
    registerValue("enemy_visuals.showDistance", Config::enemy_visuals.showDistance);
    registerValue("enemy_visuals.showWeapon", Config::enemy_visuals.showWeapon);
    registerValue("enemy_visuals.showWeaponIcon", Config::enemy_visuals.showWeaponIcon);
    registerValue("enemy_visuals.espFill", Config::enemy_visuals.espFill);
    registerValue("enemy_visuals.espThickness", Config::enemy_visuals.espThickness);
    registerValue("enemy_visuals.espFillOpacity", Config::enemy_visuals.espFillOpacity);
    registerValue("enemy_visuals.espColor", Config::enemy_visuals.espColor);
    registerValue("enemy_visuals.espColorInvisible", Config::enemy_visuals.espColorInvisible);
    registerValue("enemy_visuals.showNameTags", Config::enemy_visuals.showNameTags);
    registerValue("enemy_visuals.esp_name_avatar", Config::enemy_visuals.esp_name_avatar);
    registerValue("enemy_visuals.esp_skeleton", Config::enemy_visuals.esp_skeleton);
    registerValue("enemy_visuals.esp_skeleton_thickness", Config::enemy_visuals.esp_skeleton_thickness);
    registerValue("enemy_visuals.esp_skeleton_color", Config::enemy_visuals.esp_skeleton_color);
    registerValue("enemy_visuals.esp_skeleton_color_invisible", Config::enemy_visuals.esp_skeleton_color_invisible);
    registerValue("enemy_visuals.esp_skeleton_head", Config::enemy_visuals.esp_skeleton_head);
    registerValue("enemy_visuals.esp_vis_check", Config::enemy_visuals.esp_vis_check);
    registerValue("enemy_visuals.glow", Config::enemy_visuals.glow);
    registerValue("enemy_visuals.glow_only_visible", Config::enemy_visuals.glow_only_visible);
    registerValue("enemy_visuals.glow_color", Config::enemy_visuals.glow_color);
    registerValue("enemy_visuals.glow_color_invis", Config::enemy_visuals.glow_color_invis);
    registerValue("enemy_visuals.esp_box_style", Config::enemy_visuals.esp_box_style);
    registerValue("enemy_visuals.esp_box_width", Config::enemy_visuals.esp_box_width);
    registerValue("enemy_visuals.esp_pos_name", Config::enemy_visuals.esp_pos_name);
    registerValue("enemy_visuals.esp_pos_weapon", Config::enemy_visuals.esp_pos_weapon);
    registerValue("enemy_visuals.esp_pos_weapon_icon", Config::enemy_visuals.esp_pos_weapon_icon);
    registerValue("enemy_visuals.esp_pos_distance", Config::enemy_visuals.esp_pos_distance);
    registerValue("enemy_visuals.esp_pos_health", Config::enemy_visuals.esp_pos_health);
    registerValue("enemy_visuals.esp_pos_armor", Config::enemy_visuals.esp_pos_armor);
    registerValue("enemy_visuals.esp_bar_width", Config::enemy_visuals.esp_bar_width);
    registerValue("enemy_visuals.esp_health_auto", Config::enemy_visuals.esp_health_auto);
    registerValue("enemy_visuals.esp_health_color", Config::enemy_visuals.esp_health_color);
    registerValue("enemy_visuals.esp_armor_color", Config::enemy_visuals.esp_armor_color);
    registerValue("enemy_visuals.esp_name_color", Config::enemy_visuals.esp_name_color);
    registerValue("enemy_visuals.esp_weapon_color", Config::enemy_visuals.esp_weapon_color);
    registerValue("enemy_visuals.esp_weapon_icon_color", Config::enemy_visuals.esp_weapon_icon_color);
    registerValue("enemy_visuals.esp_distance_color", Config::enemy_visuals.esp_distance_color);
    registerValue("enemy_visuals.flag_flashed", Config::enemy_visuals.flag_flashed);
    registerValue("enemy_visuals.flag_bomb", Config::enemy_visuals.flag_bomb);
    registerValue("enemy_visuals.flag_scoped", Config::enemy_visuals.flag_scoped);
    registerValue("enemy_visuals.flag_reloading", Config::enemy_visuals.flag_reloading);
    registerValue("enemy_visuals.flag_defusing", Config::enemy_visuals.flag_defusing);
    registerValue("enemy_visuals.flag_money", Config::enemy_visuals.flag_money);
    registerValue("enemy_visuals.flag_kit", Config::enemy_visuals.flag_kit);
    registerValue("enemy_visuals.flag_helmet", Config::enemy_visuals.flag_helmet);
    registerValue("enemy_visuals.flag_nades", Config::enemy_visuals.flag_nades);
    registerValue("enemy_visuals.esp_rank", Config::enemy_visuals.esp_rank);
    registerValue("enemy_visuals.esp_3d_box", Config::enemy_visuals.esp_3d_box);
    registerValue("enemy_visuals.esp_oof", Config::enemy_visuals.esp_oof);
    registerValue("enemy_visuals.esp_oof_radius", Config::enemy_visuals.esp_oof_radius);
    registerValue("enemy_visuals.esp_oof_size", Config::enemy_visuals.esp_oof_size);
    registerValue("enemy_visuals.esp_oof_color", Config::enemy_visuals.esp_oof_color);
    registerValue("enemy_visuals.esp_3d_box_color", Config::enemy_visuals.esp_3d_box_color);
    registerValue("enemy_visuals.esp_rank_color", Config::enemy_visuals.esp_rank_color);
    registerValue("enemy_visuals.sound_esp", Config::enemy_visuals.sound_esp);
    registerValue("enemy_visuals.sound_esp_duration", Config::enemy_visuals.sound_esp_duration);
    registerValue("enemy_visuals.sound_esp_ring_size", Config::enemy_visuals.sound_esp_ring_size);
    registerValue("enemy_visuals.sound_esp_color", Config::enemy_visuals.sound_esp_color);
    registerValue("friendly_visuals.enabled", Config::friendly_visuals.enabled);
    registerValue("friendly_visuals.esp", Config::friendly_visuals.esp);
    registerValue("friendly_visuals.showHealth", Config::friendly_visuals.showHealth);
    registerValue("friendly_visuals.showArmor", Config::friendly_visuals.showArmor);
    registerValue("friendly_visuals.showDistance", Config::friendly_visuals.showDistance);
    registerValue("friendly_visuals.showWeapon", Config::friendly_visuals.showWeapon);
    registerValue("friendly_visuals.showWeaponIcon", Config::friendly_visuals.showWeaponIcon);
    registerValue("friendly_visuals.espFill", Config::friendly_visuals.espFill);
    registerValue("friendly_visuals.espThickness", Config::friendly_visuals.espThickness);
    registerValue("friendly_visuals.espFillOpacity", Config::friendly_visuals.espFillOpacity);
    registerValue("friendly_visuals.espColor", Config::friendly_visuals.espColor);
    registerValue("friendly_visuals.espColorInvisible", Config::friendly_visuals.espColorInvisible);
    registerValue("friendly_visuals.showNameTags", Config::friendly_visuals.showNameTags);
    registerValue("friendly_visuals.esp_name_avatar", Config::friendly_visuals.esp_name_avatar);
    registerValue("friendly_visuals.esp_skeleton", Config::friendly_visuals.esp_skeleton);
    registerValue("friendly_visuals.esp_skeleton_thickness", Config::friendly_visuals.esp_skeleton_thickness);
    registerValue("friendly_visuals.esp_skeleton_color", Config::friendly_visuals.esp_skeleton_color);
    registerValue("friendly_visuals.esp_skeleton_color_invisible",
                  Config::friendly_visuals.esp_skeleton_color_invisible);
    registerValue("friendly_visuals.esp_skeleton_head", Config::friendly_visuals.esp_skeleton_head);
    registerValue("friendly_visuals.esp_vis_check", Config::friendly_visuals.esp_vis_check);
    registerValue("friendly_visuals.glow", Config::friendly_visuals.glow);
    registerValue("friendly_visuals.glow_only_visible", Config::friendly_visuals.glow_only_visible);
    registerValue("friendly_visuals.glow_color", Config::friendly_visuals.glow_color);
    registerValue("friendly_visuals.glow_color_invis", Config::friendly_visuals.glow_color_invis);
    registerValue("friendly_visuals.esp_box_style", Config::friendly_visuals.esp_box_style);
    registerValue("friendly_visuals.esp_box_width", Config::friendly_visuals.esp_box_width);
    registerValue("friendly_visuals.esp_pos_name", Config::friendly_visuals.esp_pos_name);
    registerValue("friendly_visuals.esp_pos_weapon", Config::friendly_visuals.esp_pos_weapon);
    registerValue("friendly_visuals.esp_pos_weapon_icon", Config::friendly_visuals.esp_pos_weapon_icon);
    registerValue("friendly_visuals.esp_pos_distance", Config::friendly_visuals.esp_pos_distance);
    registerValue("friendly_visuals.esp_pos_health", Config::friendly_visuals.esp_pos_health);
    registerValue("friendly_visuals.esp_pos_armor", Config::friendly_visuals.esp_pos_armor);
    registerValue("friendly_visuals.esp_bar_width", Config::friendly_visuals.esp_bar_width);
    registerValue("friendly_visuals.esp_health_auto", Config::friendly_visuals.esp_health_auto);
    registerValue("friendly_visuals.esp_health_color", Config::friendly_visuals.esp_health_color);
    registerValue("friendly_visuals.esp_armor_color", Config::friendly_visuals.esp_armor_color);
    registerValue("friendly_visuals.esp_name_color", Config::friendly_visuals.esp_name_color);
    registerValue("friendly_visuals.esp_weapon_color", Config::friendly_visuals.esp_weapon_color);
    registerValue("friendly_visuals.esp_weapon_icon_color", Config::friendly_visuals.esp_weapon_icon_color);
    registerValue("friendly_visuals.esp_distance_color", Config::friendly_visuals.esp_distance_color);
    registerValue("friendly_visuals.flag_flashed", Config::friendly_visuals.flag_flashed);
    registerValue("friendly_visuals.flag_bomb", Config::friendly_visuals.flag_bomb);
    registerValue("friendly_visuals.flag_scoped", Config::friendly_visuals.flag_scoped);
    registerValue("friendly_visuals.flag_reloading", Config::friendly_visuals.flag_reloading);
    registerValue("friendly_visuals.flag_defusing", Config::friendly_visuals.flag_defusing);
    registerValue("friendly_visuals.flag_money", Config::friendly_visuals.flag_money);
    registerValue("friendly_visuals.flag_kit", Config::friendly_visuals.flag_kit);
    registerValue("friendly_visuals.flag_helmet", Config::friendly_visuals.flag_helmet);
    registerValue("friendly_visuals.flag_nades", Config::friendly_visuals.flag_nades);
    registerValue("friendly_visuals.esp_rank", Config::friendly_visuals.esp_rank);
    registerValue("friendly_visuals.esp_3d_box", Config::friendly_visuals.esp_3d_box);
    registerValue("friendly_visuals.esp_oof", Config::friendly_visuals.esp_oof);
    registerValue("friendly_visuals.esp_oof_radius", Config::friendly_visuals.esp_oof_radius);
    registerValue("friendly_visuals.esp_oof_size", Config::friendly_visuals.esp_oof_size);
    registerValue("friendly_visuals.esp_oof_color", Config::friendly_visuals.esp_oof_color);
    registerValue("friendly_visuals.esp_3d_box_color", Config::friendly_visuals.esp_3d_box_color);
    registerValue("friendly_visuals.esp_rank_color", Config::friendly_visuals.esp_rank_color);
    registerValue("friendly_visuals.sound_esp", Config::friendly_visuals.sound_esp);
    registerValue("friendly_visuals.sound_esp_duration", Config::friendly_visuals.sound_esp_duration);
    registerValue("friendly_visuals.sound_esp_ring_size", Config::friendly_visuals.sound_esp_ring_size);
    registerValue("friendly_visuals.sound_esp_color", Config::friendly_visuals.sound_esp_color);
    registerValue("movement.bhop", Config::movement.bhop);
    registerValue("movement.airstrafe", Config::movement.airstrafe);
    registerValue("movement.edgebug", Config::movement.edgebug);
    registerValue("movement.edgejump", Config::movement.edgejump);
    registerValue("movement.edgestop", Config::movement.edgestop);
    registerValue("movement.fastladder", Config::movement.fastladder);
    registerValue("movement.jumpbug", Config::movement.jumpbug);
    registerValue("movement.slowwalk", Config::movement.slowwalk);
    registerValue("movement.airstop", Config::movement.airstop);
    registerValue("movement.edgebug_jump_steps", Config::movement.edgebug_jump_steps);
    registerValue("movement.edgebug_mode", Config::movement.edgebug_mode);
    registerValue("movement.edgebug_passes", Config::movement.edgebug_passes);
    registerValue("movement.slowwalk_speed", Config::movement.slowwalk_speed);
    registerValue("movement.quick_peek", Config::movement.quick_peek);
    registerValue("movement.duck_peek", Config::movement.duck_peek);
    registerValue("movement.peek_on_shot", Config::movement.peek_on_shot);
    registerValue("movement.peek_on_release", Config::movement.peek_on_release);
    registerValue("movement.peek_filled", Config::movement.peek_filled);
    registerValue("movement.peek_radius", Config::movement.peek_radius);
    registerValue("movement.peek_glow", Config::movement.peek_glow);
    registerValue("movement.peek_color", Config::movement.peek_color);
    registerValue("movement.peek_tolerance", Config::movement.peek_tolerance);
    registerValue("movement.peek_stop", Config::movement.peek_stop);
    registerValue("movement.peek_dt_same_target", Config::movement.peek_dt_same_target);
    registerValue("melee.knife", Config::melee.knife);
    registerValue("melee.knife_dt", Config::melee.knife_dt);
    registerValue("melee.zeus", Config::melee.zeus);
    registerValue("melee.knife_attack", Config::melee.knife_attack);
    registerValue("melee.zeus_lethal", Config::melee.zeus_lethal);

    registerValue("movement.turn_balance", Config::movement.turn_balance);
    registerValue("movement.turn_radius", Config::movement.turn_radius);
    addBinding("thirdperson", VK_MBUTTON, KeyMode::Toggle);
}

BindingTarget* Keybinds::find(void* value) {
    initialize();
    for (auto& [id, target] : targets_)
        if (target.resolve() == value)
            return &target;
    return nullptr;
}

BindingTarget* Keybinds::find(const std::string& id) {
    initialize();
    auto it = targets_.find(id);
    return it == targets_.end() ? nullptr : &it->second;
}

void Keybinds::apply(BindingTarget& target) {
    const KeyBinding* winner = nullptr;
    for (const auto& binding : target.bindings)
        if (binding.active && (!winner || binding.order > winner->order))
            winner = &binding;
    const bool changed = !Equal(Read(target), winner ? winner->value : target.base);
    target.overriding = winner != nullptr;
    target.applied = winner ? winner->value : target.base;
    Write(target, target.applied);
    if (changed && target.changed)
        target.changed();
}

void Keybinds::reconcile() {
    for (auto& [id, target] : targets_) {
        if (target.bindings.empty())
            continue;
        if (!target.resolve())
            continue;
        if (target.eligible && !target.eligible() && target.overriding) {
            for (auto& binding : target.bindings)
                binding.active = false;
            apply(target);
        }
        const auto current = Read(target);
        if (!target.overriding)
            target.base = target.applied = current;
        else if (!Equal(current, target.applied)) {
            target.base = current;
            apply(target);
        }
    }
}

void Keybinds::manualChange(void* value) {
    if (auto* target = find(value)) {
        BindingTarget edited;
        edited.kind = target->kind;
        edited.resolve = [value] {
            return value;
        };
        target->base = Read(edited);
        apply(*target);
    }
}

void Keybinds::manualChange(const std::string& id, const BindValue& value) {
    if (auto* target = find(id)) {
        target->base = value;
        apply(*target);
    }
}

KeyBinding& Keybinds::addBinding(const std::string& id, int key, KeyMode mode, BindValue value) {
    auto* target = find(id);
    if (target->kind == BindKind::Boolean)
        value = Read(*target);
    else if (value.index() != static_cast<size_t>(target->kind))
        value = Read(*target);
    target->bindings.push_back({nextId_++, key, mode, value});
    return target->bindings.back();
}

void Keybinds::removeBinding(const std::string& id, std::uint64_t binding) {
    if (auto* target = find(id)) {
        if (listening_ == binding)
            listening_ = 0;
        std::erase_if(target->bindings, [&](const auto& b) {
            return b.id == binding;
        });
        apply(*target);
    }
}

void Keybinds::processKeyEvent(int key, bool down, bool allowActivation) {
    initialize();
    if (key <= 0 || key >= 256 || down_[key] == down)
        return;
    down_[key] = down;
    for (auto& [id, target] : targets_)
        for (auto& binding : target.bindings) {
            if (binding.key != key)
                continue;
            bool activate = false, deactivate = false;
            if (binding.mode == KeyMode::Hold) {
                activate = down && allowActivation && (!target.eligible || target.eligible());
                deactivate = !down;
            } else if (down && allowActivation && (!target.eligible || target.eligible())) {
                activate = !binding.active;
                deactivate = binding.active;
            }
            if (activate) {
                if (!target.overriding)
                    target.base = Read(target);
                if (target.kind == BindKind::Boolean)
                    binding.value = !std::get<bool>(Read(target));
                binding.active = true;
                binding.order = nextOrder_++;
                apply(target);
            } else if (deactivate && binding.active) {
                binding.active = false;
                apply(target);
            }
        }
}

bool Keybinds::blocked(int key) {
    return key == VK_INSERT || key == VK_ESCAPE || key == VK_F4;
}

void Keybinds::onWindowMessage(unsigned msg, std::uintptr_t wParam, std::intptr_t lParam) {
    if (msg == WM_INPUT) {
        RAWINPUT input{};
        UINT size = sizeof(input);
        if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, &input, &size, sizeof(RAWINPUTHEADER)) ==
                UINT(-1) ||
            input.header.dwType != RIM_TYPEMOUSE)
            return;
        const auto flags = input.data.mouse.usButtonFlags;
        if (!(flags & (RI_MOUSE_BUTTON_4_DOWN | RI_MOUSE_BUTTON_4_UP | RI_MOUSE_BUTTON_5_DOWN | RI_MOUSE_BUTTON_5_UP)))
            return;
        std::lock_guard lock(eventsMutex_);
        if (flags & (RI_MOUSE_BUTTON_4_DOWN | RI_MOUSE_BUTTON_4_UP))
            rawKeys_[VK_XBUTTON1] = true;
        if (flags & (RI_MOUSE_BUTTON_5_DOWN | RI_MOUSE_BUTTON_5_UP))
            rawKeys_[VK_XBUTTON2] = true;
        if (flags & RI_MOUSE_BUTTON_4_DOWN)
            events_.push_back({VK_XBUTTON1, true, true});
        if (flags & RI_MOUSE_BUTTON_4_UP)
            events_.push_back({VK_XBUTTON1, false, true});
        if (flags & RI_MOUSE_BUTTON_5_DOWN)
            events_.push_back({VK_XBUTTON2, true, true});
        if (flags & RI_MOUSE_BUTTON_5_UP)
            events_.push_back({VK_XBUTTON2, false, true});
        return;
    }
    int key = 0;
    bool down = false;
    switch (msg) {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        key = static_cast<int>(wParam);
        down = true;
        break;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        key = static_cast<int>(wParam);
        break;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        key = VK_LBUTTON;
        down = true;
        break;
    case WM_LBUTTONUP:
        key = VK_LBUTTON;
        break;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONDBLCLK:
        key = VK_RBUTTON;
        down = true;
        break;
    case WM_RBUTTONUP:
        key = VK_RBUTTON;
        break;
    case WM_MBUTTONDOWN:
    case WM_MBUTTONDBLCLK:
        key = VK_MBUTTON;
        down = true;
        break;
    case WM_MBUTTONUP:
        key = VK_MBUTTON;
        break;
    case WM_XBUTTONDOWN:
    case WM_XBUTTONDBLCLK:
        key = HIWORD(wParam) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2;
        down = true;
        break;
    case WM_XBUTTONUP:
        key = HIWORD(wParam) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2;
        break;
    default:
        return;
    }
    if (key <= 0 || key >= 256)
        return;
    std::lock_guard lock(eventsMutex_);
    if (!rawKeys_[key])
        events_.push_back({key, down});
}

void Keybinds::pollInputs(bool menuOpen) {
    initialize();
    reconcile();
    std::vector<KeyEvent> events;
    std::array<bool, 256> rawKeys;
    {
        std::lock_guard lock(eventsMutex_);
        events.swap(events_);
        rawKeys = rawKeys_;
    }
    // Raw and legacy copies can arrive in different frames or as whole click
    // pairs. State-only deduplication cannot distinguish them from two clicks.
    std::erase_if(events, [&](const KeyEvent& event) {
        return rawKeys[event.key] && !event.raw;
    });
    std::array<bool, 256> watched{};
    if (isListening())
        watched.fill(true);
    else {
        for (const auto& [id, target] : targets_)
            for (const auto& binding : target.bindings)
                if (binding.key > 0 && binding.key < 256)
                    watched[binding.key] = true;
        for (const auto& event : events)
            watched[event.key] = true;
    }
    std::array<bool, 256> physical{};
    for (int i = 1; i < 256; i++)
        if (watched[i] || down_[i])
            physical[i] = (GetAsyncKeyState(i) & 0x8000) != 0;
    if (!sampled_) {
        down_ = physical;
        for (const auto& event : events)
            down_[event.key] = false;
        sampled_ = true;
    }
    DWORD foregroundPid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &foregroundPid);
    const bool allow = !menuOpen && !isListening() && foregroundPid == GetCurrentProcessId();
    std::vector<int> pressed;
    std::array<bool, 256> received{};
    for (const auto& event : events) {
        received[event.key] = true;
        if (event.down && !down_[event.key])
            pressed.push_back(event.key);
        processKeyEvent(event.key, event.down, allow);
    }
    // A queued transition is newer than the preceding frame's key state.
    // Do not synthesize another edge for that key in the same input batch.
    for (int i = 1; i < 256; i++)
        if (!rawKeys[i] && !received[i] && physical[i] != down_[i]) {
            if (physical[i])
                pressed.push_back(i);
            processKeyEvent(i, physical[i], allow);
        }
    // A raw-input window may not receive button-up after losing focus.
    // Release Hold bindings without generating Toggle presses on focus loss.
    if (foregroundPid != GetCurrentProcessId())
        for (int i = 1; i < 256; i++)
            if (rawKeys[i] && down_[i])
                processKeyEvent(i, false, false);
    if (listening_) {
        if (!menuOpen) {
            listening_ = 0;
            return;
        }
        // Only the click that armed this editor must be released. Holding a
        // movement key must not prevent a different key from being captured.
        const bool releasedClick = waitRelease_ && !physical[VK_LBUTTON];
        if (releasedClick)
            waitRelease_ = false;
        if (!waitRelease_)
            for (int key : pressed) {
                if (releasedClick && key == VK_LBUTTON)
                    continue;
                if (key == VK_ESCAPE) {
                    listening_ = 0;
                    break;
                }
                if (blocked(key))
                    continue;
                for (auto& [id, target] : targets_)
                    for (auto& binding : target.bindings)
                        if (binding.id == listening_) {
                            binding.active = false;
                            binding.key = key;
                            apply(target);
                        }
                listening_ = 0;
                break;
            }
    }
}

void Keybinds::clearAllToggles() {
    for (auto& [id, target] : targets_) {
        if (target.overriding) {
            for (auto& b : target.bindings)
                b.active = false;
            apply(target);
        }
    }
    listening_ = 0;
    waitRelease_ = false;
}

void Keybinds::resetToDefaults() {
    initialize();
    for (auto& [id, target] : targets_) {
        target.bindings.clear();
        target.overriding = false;
        target.base = target.applied = Read(target);
    }
    listening_ = 0;
    addBinding("thirdperson", VK_MBUTTON, KeyMode::Toggle);
}

int Keybinds::getKey(bool& feature) const {
    for (const auto& [id, target] : targets_)
        if (target.resolve() == &feature)
            return target.bindings.empty() ? 0 : target.bindings.front().key;
    return 0;
}

int Keybinds::getMode(bool& feature) const {
    for (const auto& [id, target] : targets_)
        if (target.resolve() == &feature)
            return target.bindings.empty() ? 0 : static_cast<int>(target.bindings.front().mode);
    return 0;
}

void Keybinds::setKey(bool& feature, int key) {
    if (auto* target = find(&feature)) {
        if (target->bindings.empty())
            addBinding(target->id, key, KeyMode::Hold);
        else {
            target->bindings.front().key = key;
            target->bindings.front().active = false;
        }
    }
}

void Keybinds::setMode(bool& feature, int mode) {
    if (auto* target = find(&feature)) {
        if (mode == 0) {
            target->bindings.clear();
            return;
        }
        if (target->bindings.empty())
            addBinding(target->id, 0, KeyMode::Hold);
        target->bindings.front().mode = mode == 2 ? KeyMode::Toggle : KeyMode::Hold;
    }
}

void Keybinds::formatKeyName(int key, char* out, size_t size) {
    const char* mouse = nullptr;
    switch (key) {
    case 0:
        mouse = "None";
        break;
    case VK_LBUTTON:
        mouse = "M1";
        break;
    case VK_RBUTTON:
        mouse = "M2";
        break;
    case VK_MBUTTON:
        mouse = "M3";
        break;
    case VK_XBUTTON1:
        mouse = "M4";
        break;
    case VK_XBUTTON2:
        mouse = "M5";
        break;
    }
    if (mouse) {
        strncpy_s(out, size, mouse, _TRUNCATE);
        return;
    }
    const UINT scan = MapVirtualKeyA(key, MAPVK_VK_TO_VSC);
    if (scan && GetKeyNameTextA(static_cast<LONG>(scan << 16), out, static_cast<int>(size)))
        return;
    sprintf_s(out, size, "0x%02X", key);
}

void Keybinds::editor(BindingTarget& target) {
    const float dpi = ImGui::GetFontSize() / 16.f;
    UiText::TextUnformatted(target.label.c_str());
    ImGui::Separator();
    auto base = target.overriding ? target.base : Read(target);
    if (EditValue("Base value", target, base)) {
        target.base = base;
        apply(target);
    }
    UiText::TextDisabled("Last activated binding wins. Release/deactivate restores the base.");
    std::uint64_t removed = 0;
    for (auto& binding : target.bindings) {
        ImGui::PushID(static_cast<int>(binding.id));
        char keyName[32];
        formatKeyName(binding.key, keyName, sizeof(keyName));
        if (UiText::Button(listening_ == binding.id ? "Press a key..." : keyName, ImVec2(110 * dpi, 0))) {
            listening_ = binding.id;
            waitRelease_ = true;
        }
        ImGui::SameLine();
        int mode = binding.mode == KeyMode::Toggle ? 1 : 0;
        const char* modes[] = {"Hold", "Toggle"};
        ImGui::SetNextItemWidth(100 * dpi);
        if (UiText::Combo("##mode", &mode, modes, 2)) {
            binding.active = false;
            binding.mode = mode ? KeyMode::Toggle : KeyMode::Hold;
            apply(target);
        }
        ImGui::SameLine();
        if (UiText::SmallButton("Remove"))
            removed = binding.id;
        if (target.kind != BindKind::Boolean) {
            if (EditValue("Value", target, binding.value) && binding.active)
                apply(target);
        } else
            UiText::TextDisabled("Invert the effective switch value on activation");
        ImGui::PopID();
    }
    if (removed)
        removeBinding(target.id, removed);
    if (UiText::Button("Add binding"))
        addBinding(target.id, 0, KeyMode::Hold, Read(target));
    UiText::TextDisabled("Insert / Esc / F4 are reserved.");
}

void Keybinds::contextMenu(void* value, const char* label, bool bounded, float low, float high) {
    auto* target = find(value);
    if (!target)
        return;
    target->bounded = bounded;
    target->minimum = low;
    target->maximum = high;
    contextMenu(target->id, label);
}

void Keybinds::contextMenu(const std::string& id, const char* label) {
    auto* target = find(id);
    if (!target)
        return;
    std::string prefix;
    if (id.starts_with("legitbot."))
        prefix = target->label.substr(0, target->label.rfind(" / ") + 3);
    else if (id.starts_with("enemy_visuals."))
        prefix = "Enemy / ";
    else if (id.starts_with("friendly_visuals."))
        prefix = "Friendly / ";
    if (label && label[0] && !(label[0] == '#' && label[1] == '#'))
        target->label = prefix + std::string(std::string_view(label).substr(0, std::string_view(label).find("##")));
    const std::string popup = "##keybinds/" + target->id;
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
        ImGui::OpenPopup(popup.c_str());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(12 * ImGui::GetFontSize() / 16.f, 12 * ImGui::GetFontSize() / 16.f));
    if (ImGui::BeginPopup(popup.c_str())) {
        const bool disabled = (GImGui->CurrentItemFlags & ImGuiItemFlags_Disabled) != 0;
        if (disabled)
            ImGui::BeginDisabledOverrideReenable();
        if (ImGui::GetCurrentWindow()->BeginCount == 1)
            editor(*target);
        if (disabled)
            ImGui::EndDisabledOverrideReenable();
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
}

nlohmann::json Keybinds::save() {
    initialize();
    reconcile();
    nlohmann::json j = nlohmann::json::array();
    for (const auto& [id, target] : targets_) {
        if (target.bindings.empty())
            continue;
        nlohmann::json bindings = nlohmann::json::array();
        for (const auto& b : target.bindings)
            bindings.push_back({{"key", b.key}, {"mode", static_cast<int>(b.mode)}, {"value", JsonValue(b.value)}});
        j.push_back({{"target", id},
                     {"base", JsonValue(target.overriding ? target.base : Read(target))},
                     {"bindings", bindings}});
    }
    return j;
}

void Keybinds::load(const nlohmann::json& json) {
    initialize();
    clearAllToggles();
    for (auto& [id, target] : targets_) {
        target.bindings.clear();
        target.base = target.applied = Read(target);
    }
    if (!json.is_array())
        return;
    std::map<std::string, BindValue> damageBases;
    for (const auto& [id, target] : targets_)
        if (id.starts_with("rage.") && id.ends_with(".damage"))
            damageBases.emplace(id, Read(target));
    for (const auto& entry : json) {
        try {
            auto id = entry.at("target").get<std::string>();
            if (id.starts_with("rage.") && id != "rage.enabled" && id.ends_with(".enabled")) {
                if (auto* target = find("rage.enabled")) {
                    auto migrated = entry;
                    migrated["target"] = "rage.enabled";
                    migrated["base"] = JsonValue(target->base);
                    loadTarget(*target, migrated);
                }
                continue;
            }
            if (id.starts_with("rage.") && (id.ends_with(".min_damage") || id.ends_with(".lethal_margin"))) {
                const bool lethal = id.ends_with(".lethal_margin");
                id = id.substr(0, id.rfind('.')) + ".damage";
                if (auto* target = find(id)) {
                    auto migrated = entry;
                    migrated["target"] = id;
                    migrated["base"] = JsonValue(damageBases.at(id));
                    for (auto& binding : migrated["bindings"])
                        binding["value"] = lethal ? 100 + std::clamp(binding["value"].get<int>(), 1, 20)
                                                  : std::clamp(binding["value"].get<int>(), 0, 100);
                    loadTarget(*target, migrated);
                }
                continue;
            }
            // Weapon-local aim/AA bindings now address the shared Global panel.
            // Prefer an explicit Global entry when the old configuration has both.
            if (!find(id) && id.starts_with("rage.")) {
                const auto field = id.find('.', 5);
                if (field != std::string::npos) {
                    const auto shared = "rage.0." + id.substr(field + 1);
                    if (auto* target = find(shared);
                        target && std::none_of(json.begin(), json.end(), [&](const auto& value) {
                            return value.is_object() && value.value("target", std::string()) == shared;
                        })) {
                        auto migrated = entry;
                        migrated["target"] = shared;
                        migrated["base"] = JsonValue(target->base);
                        loadTarget(*target, migrated);
                    }
                }
            }
            if (auto* target = find(id))
                loadTarget(*target, entry);
        } catch (const nlohmann::json::exception&) {
        } catch (const std::invalid_argument&) {
        }
    }
}

void Keybinds::loadTarget(BindingTarget& target, const nlohmann::json& entry) {
    target.base = ReadJson(target.kind, entry.at("base"));
    apply(target);
    for (const auto& item : entry.at("bindings")) {
        const int key = item.at("key").get<int>(), mode = item.at("mode").get<int>();
        if (key < 0 || key > 255 || (key && blocked(key)) || (mode != 1 && mode != 2))
            continue;
        // Several old weapon-local switches can share a key. Once merged into
        // the global switch, one physical press must not invert it twice.
        // Also repair configs already saved by the previous migration.
        if (target.id == "rage.enabled" &&
            std::any_of(target.bindings.begin(), target.bindings.end(), [&](const KeyBinding& binding) {
                return binding.key == key && static_cast<int>(binding.mode) == mode;
            }))
            continue;
        addBinding(target.id, key, static_cast<KeyMode>(mode), ReadJson(target.kind, item.at("value")));
    }
}

int Keybinds::listSnapshots(KeybindSnapshot* out, int maxOut) const {
    int n = 0;
    for (const auto& [id, target] : targets_)
        for (const auto& binding : target.bindings) {
            if (!binding.key || n >= maxOut)
                continue;
            auto& row = out[n++];
            row = {};
            row.name = target.label.c_str();
            row.key = binding.key;
            row.mode = static_cast<int>(binding.mode);
            row.active = binding.active;
            row.enabled = true;
            if (target.kind == BindKind::Float)
                row.value = std::get<float>(binding.value);
            else if (target.kind == BindKind::Integer)
                row.value = static_cast<float>(std::get<int>(binding.value));
        }
    return n;
}

Keybinds keybind;
