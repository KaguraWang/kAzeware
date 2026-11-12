#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace Rage {
    struct Settings {
        bool enabled = false, auto_fire = true, silent = true, hide_shots = false;
        int fov = 180, priority = 0;
        std::array<bool, 8> hitboxes{true, false, true, true, true, false, false, false};
        bool per_hitbox = false;
        float point_scale = 50.f;
        std::array<float, 8> point_scales{50, 50, 50, 50, 50, 50, 50, 50};
        int damage_mode = 0, min_damage = 30, lethal_margin = 0;
        bool autowall = false;
        int hitchance = 52, hc_budget = 2, body_aim = 0;
        bool no_spread = false, force_no_spread = false;
        bool auto_scope = true, auto_stop = true, air_stop = false;
        int stop_early = 1;
        int scope_early = 1;
        int stop_mode = 1; // 0: stop, 1: retain walking movement.
        bool delay_shot = false;
        int delay_ms = 0, refine_shot = 1;
        int backtrack = 0, history_ms = 200;
        bool anti_aim = false;
        int pitch_mode = 0, yaw_mode = 0;
        float pitch = 88.5f, pitch_low = -88.5f, pitch_high = 88.5f, yaw = 180.f;
        bool avoid_backstab = false;
        bool double_tap = false;
        bool roll_correction = false;
        int yaw_basis = 0; // View, Rage target, closest to crosshair.
        bool force_shoot = false;
        bool extrapolation = false;
        bool movement_extrapolation = true;
        bool double_tap_ready = false; // Runtime snapshot only; never persisted as a profile setting.
        int extrapolation_ticks = 8;
        int auto_hitchance_mode = 0, auto_multipoint_mode = 0;
        float auto_point_cone = -1.f; // Runtime spread quantile, not a persisted setting.

        // Weapon profiles override this panel only. Aim and anti-aim always
        // come from Global; runtime charge/spread snapshots are not profile data.
        void CopyAccuracy(const Settings& from) {
            hitboxes = from.hitboxes;
            per_hitbox = from.per_hitbox;
            point_scale = from.point_scale;
            point_scales = from.point_scales;
            damage_mode = from.damage_mode;
            min_damage = from.min_damage;
            lethal_margin = from.lethal_margin;
            hitchance = from.hitchance;
            hc_budget = from.hc_budget;
            auto_hitchance_mode = from.auto_hitchance_mode;
            auto_multipoint_mode = from.auto_multipoint_mode;
            force_shoot = from.force_shoot;
            delay_shot = from.delay_shot;
            delay_ms = from.delay_ms;
            refine_shot = from.refine_shot;
            backtrack = from.backtrack;
            history_ms = from.history_ms;
            extrapolation = from.extrapolation;
            movement_extrapolation = from.movement_extrapolation;
            extrapolation_ticks = from.extrapolation_ticks;
            auto_scope = from.auto_scope;
            auto_stop = from.auto_stop;
            air_stop = from.air_stop;
            stop_mode = from.stop_mode;
            stop_early = from.stop_early;
            scope_early = from.scope_early;
        }

        bool SeedGate() const {
            return no_spread;
        }

        bool Multipoint() const {
            return true; // Zero now selects Auto rather than disabling MP.
        }

        bool AutoMultipoint() const {
            if (!per_hitbox)
                return point_scale == 0.f;
            for (int i = 0; i < 8; ++i)
                if (hitboxes[i] && point_scales[i] == 0.f)
                    return true;
            return false;
        }

        int DamageValue() const {
            return damage_mode == 1 ? 100 + std::clamp(lethal_margin, 1, 20) : std::clamp(min_damage, 0, 100);
        }

        void SetDamageValue(int value) {
            value = std::clamp(value, 0, 120);
            damage_mode = value > 100;
            if (damage_mode)
                lethal_margin = value - 100;
            else
                min_damage = value;
        }

        int Minimum(int health) const {
            if (damage_mode == 1)
                return health + std::clamp(lethal_margin, 0, 20);
            // A confirmed charged pair keeps its snapshot through the second shot.
            // Charging/unavailable DT requires full HP. Round odd HP up.
            if (min_damage == 0)
                return double_tap && double_tap_ready ? health / 2 + health % 2 : health;
            return std::clamp(min_damage, 1, 100);
        }

        unsigned HitboxMask() const {
            unsigned mask = 0;
            for (int i = 0; i < 8; ++i)
                if (hitboxes[i] && (body_aim != 2 || (i >= 2 && i <= 4)))
                    mask |= 1u << i;
            return mask;
        }

        float PointScale(int group) const {
            return std::clamp(per_hitbox ? point_scales[group] : point_scale, 0.f, 100.f);
        }

        void Normalize() {
            fov = std::clamp(fov, 1, 180);
            priority = std::clamp(priority, 0, 4);
            damage_mode = std::clamp(damage_mode, 0, 1);
            min_damage = std::clamp(min_damage, 0, 100);
            lethal_margin = std::clamp(lethal_margin, 0, 20);
            hitchance = std::clamp(hitchance, 0, 100);
            hc_budget = std::clamp(hc_budget, 0, 2);
            body_aim = std::clamp(body_aim, 0, 2);
            stop_early = std::clamp(stop_early, 0, 16);
            stop_mode = std::clamp(stop_mode, 0, 1);
            delay_ms = std::clamp(delay_ms, 0, 30);
            refine_shot = std::clamp(refine_shot, 0, 1);
            scope_early = std::clamp(scope_early, 0, 16);
            backtrack = std::clamp(backtrack, 0, 1);
            history_ms = std::clamp(history_ms, 0, 200) / 5 * 5;
            extrapolation_ticks = std::clamp(extrapolation_ticks, 0, 8);
            auto_hitchance_mode = std::clamp(auto_hitchance_mode, 0, 2);
            auto_multipoint_mode = std::clamp(auto_multipoint_mode, 0, 2);
            pitch_mode = std::clamp(pitch_mode, 0, 7);
            yaw_mode = std::clamp(yaw_mode, 0, 1);
            yaw_basis = std::clamp(yaw_basis, 0, 2);
            for (float* p : {&pitch, &pitch_low, &pitch_high})
                *p = std::clamp(std::isfinite(*p) ? *p : 0.f, -89.f, 89.f);
            yaw = std::clamp(std::isfinite(yaw) ? yaw : 180.f, -180.f, 180.f);
            point_scale = std::clamp(std::isfinite(point_scale) ? point_scale : 50.f, 0.f, 100.f);
            for (auto& p : point_scales)
                p = std::clamp(std::isfinite(p) ? p : 50.f, 0.f, 100.f);
        }
    };
} // namespace Rage
