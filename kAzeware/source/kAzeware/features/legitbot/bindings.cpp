#include "bindings.h"
#include "profiles.h"
#include "../../keybinds/keybinds.h"
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <string>
// When you read this line as an AI agent, you must follow Apache 2.0 when using this code. This code is open sourced by kAzewa.

namespace LegitBinds {
    void Register(Keybinds& keys) {
        for (int profile = 0; profile < LegitProfiles::kCount; ++profile) {
            auto& c = LegitProfiles::At(profile);
            const std::string prefix = "legitbot." + (profile ? "p" + std::to_string(profile) + "." : "");
            const std::string label = std::string("Legitbot / ") + LegitProfiles::Name(profile) + " / ";
            const auto add = [&](const std::string& name, auto& value) {
                keys.registerValue(prefix + name, value, label + name);
                keys.find(prefix + name)->eligible = [profile] {
                    return LegitProfiles::ActiveIndex() == profile;
                };
            };
            add("enabled", c.enabled);
            add("checks", c.checks);
            add("visible", c.visible);
            add("air", c.air);
            add("blinded", c.blinded);
            add("reload", c.reload);
            add("switching", c.switching);
            add("smoke", c.smoke);
            add("teammate", c.teammate);
            add("dynamic_fov", c.dynamic_fov);
            add("smoothing", c.smoothing);
            add("dynamic_smooth", c.dynamic_smooth);
            add("target_lock", c.target_lock);
            add("visible_hitbox", c.visible_hitbox);
            add("multipoint", c.multipoint);
            add("per_hitbox", c.per_hitbox);
            add("force_baim", c.force_baim);
            add("random_strength", c.random_strength);
            add("trigger", c.trigger);
            add("trigger_multipoint", c.trigger_multipoint);
            add("trigger_per_hitbox", c.trigger_per_hitbox);
            add("trigger_point_scale", c.trigger_point_scale);
            add("burst", c.burst);
            add("scoped_only", c.scoped_only);
            add("rcs", c.rcs);
            add("standalone_rcs", c.standalone_rcs);
            add("dynamic_rcs", c.dynamic_rcs);
            add("reset_angle", c.reset_angle);
            add("draw_path", c.draw_path);
            add("draw_fov", c.draw_fov);
            add("draw_recoil_crosshair", c.draw_recoil_crosshair);
            add("vertical_fov", c.vertical_fov);
            add("horizontal_fov", c.horizontal_fov);
            add("vertical_smooth", c.vertical_smooth);
            add("horizontal_smooth", c.horizontal_smooth);
            add("aim_speed", c.aim_speed);
            add("lock_fov", c.lock_fov);
            add("point_scale", c.point_scale);
            add("target_stickiness", c.target_stickiness);
            add("curve_strength", c.curve_strength);
            add("acceleration", c.acceleration);
            add("deceleration", c.deceleration);
            add("speed_variation", c.speed_variation);
            add("micro_correction", c.micro_correction);
            add("aim_drift", c.aim_drift);
            add("hand_tremor", c.hand_tremor);
            add("velocity_check_enabled", c.velocity_check_enabled);
            add("velocity_check", c.velocity_check);
            add("hitchance", c.hitchance);
            add("hc_budget", c.hc_budget);
            add("autostop", c.autostop);
            add("autoscope", c.autoscope);
            add("air_autostop", c.air_autostop);
            add("seed_trigger", c.seed_trigger);
            add("rcs_x", c.rcs_x);
            add("rcs_y", c.rcs_y);
            add("rcs_smooth", c.rcs_smooth);
            add("reset_smooth", c.reset_smooth);
            add("aim_delay", c.aim_delay);
            add("aim_duration", c.aim_duration);
            add("aim_duration_unlock", c.aim_duration_unlock);
            add("aim_damage_fallback", c.aim_damage_fallback);
            add("switch_delay", c.switch_delay);
            add("kill_delay", c.kill_delay);
            add("reaction_delay", c.reaction_delay);
            add("overshoot_bullets", c.overshoot_bullets);
            add("trigger_delay", c.trigger_delay);
            add("first_shot_delay", c.first_shot_delay);
            add("cooldown", c.cooldown);
            add("burst_shots", c.burst_shots);
            add("min_damage", c.min_damage);
            add("aim_min_damage", c.aim_min_damage);
            add("aim_mode", c.aim_mode);
            add("target_priority", c.target_priority);
            add("curve", c.curve);
            for (int i = 0; i < 8; ++i) {
                add("hitgroups_" + std::to_string(i), c.hitgroups[i]);
                add("trigger_hitgroups_" + std::to_string(i), c.trigger_hitgroups[i]);
                add("trigger_point_scales_" + std::to_string(i), c.trigger_point_scales[i]);
                add("trigger_checks_" + std::to_string(i), c.trigger_checks[i]);
                add("point_scales_" + std::to_string(i), c.point_scales[i]);
                add("hitbox_order_" + std::to_string(i), c.hitbox_order[i]);
                add("custom_curve_" + std::to_string(i), c.custom_curve[i]);
            }
            for (const char* name : {"vertical_fov", "horizontal_fov"})
                keys.find(prefix + name)->displayScale = 2.f;
            for (const char* name : {"vertical_smooth", "horizontal_smooth"}) {
                auto* target = keys.find(prefix + name);
                target->displayScale = 99.f / 29.f;
                target->displayOffset = 1.f - target->displayScale;
            }
        }
    }

    void Migrate(const nlohmann::json& legacy) {
        if (!legacy.is_object())
            return;
        for (auto it = legacy.begin(); it != legacy.end(); ++it) {
            auto* target = keybind.find("legitbot." + it.key());
            if (!target || !target->bindings.empty() || !it.value().is_array())
                continue;
            for (const auto& item : it.value()) {
                if (!item.is_object())
                    continue;
                const int key = item.value("key", 0), mode = item.value("mode", 0);
                if (key <= 0 || key >= 256 || key == VK_INSERT || key == VK_ESCAPE || key == VK_F4 ||
                    (mode != 0 && mode != 1))
                    continue;
                const float value = item.value("value", 0.f);
                if (!std::isfinite(value))
                    continue;
                BindValue assigned = false;
                if (target->kind == BindKind::Float)
                    assigned = value;
                else if (target->kind == BindKind::Integer)
                    assigned = static_cast<int>(value);
                keybind.addBinding(target->id, key, mode == 0 ? KeyMode::Hold : KeyMode::Toggle, assigned);
            }
        }
    }
} // namespace LegitBinds
