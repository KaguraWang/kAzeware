#pragma once
#include "../../../external/imgui/imgui.h"
#include <atomic>
#include <cstdint>
#include <map>
#include <array>
#include <cstring>
#include <algorithm>
#include <cmath>
class C_CSWeaponBase;

// CBA to make proper atm, it's 03:42 right now.
// For now just stores config values don't mind it too much
// (FYI THIS IS A HORRID SOLUTION BUT FUNCTIONS)
namespace Config {
    // 配置页编辑的视觉参数；默认值在成员声明处给出。
    enum EspBoxStyle : int { ESP_BOX_FULL = 0, ESP_BOX_CORNER = 1 };

    enum EspElemPos : int { ESP_POS_TOP = 0, ESP_POS_BOTTOM = 1, ESP_POS_LEFT = 2, ESP_POS_RIGHT = 3 };

    struct PlayerVisualSettings {
        bool enabled = false;
        bool esp = false;
        bool showHealth = false;
        bool showArmor = false;
        bool showDistance = false;
        bool showWeapon = false;
        bool showWeaponIcon = false;
        bool espFill = false;
        float espThickness = 1.0f;
        float espFillOpacity = 0.5f;
        ImVec4 espColor = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
        ImVec4 espColorInvisible = ImVec4(0.45f, 0.55f, 1.0f, 1.0f);
        bool showNameTags = false;
        bool esp_name_avatar = false;
        bool esp_skeleton = false;
        float esp_skeleton_thickness = 1.5f;
        ImVec4 esp_skeleton_color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        ImVec4 esp_skeleton_color_invisible = ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
        bool esp_skeleton_head = true;
        bool esp_vis_check = true;
        bool glow = false;
        bool glow_only_visible = false;
        ImVec4 glow_color = ImVec4(0.25f, 0.85f, 1.f, 1.f);
        ImVec4 glow_color_invis = ImVec4(1.f, 0.35f, 0.85f, 1.f);
        int esp_box_style = ESP_BOX_FULL;
        float esp_box_width = 0.42f;
        int esp_pos_name = ESP_POS_TOP;
        int esp_pos_weapon = ESP_POS_BOTTOM;
        int esp_pos_weapon_icon = ESP_POS_BOTTOM;
        int esp_pos_distance = ESP_POS_BOTTOM;
        int esp_pos_health = ESP_POS_LEFT;
        int esp_pos_armor = ESP_POS_LEFT;
        float esp_bar_width = 3.0f;
        bool esp_health_auto = true;
        ImVec4 esp_health_color = ImVec4(0.20f, 0.90f, 0.25f, 1.0f);
        ImVec4 esp_armor_color = ImVec4(0.27f, 0.63f, 1.0f, 1.0f);
        ImVec4 esp_name_color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        ImVec4 esp_weapon_color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        ImVec4 esp_weapon_icon_color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        ImVec4 esp_distance_color = ImVec4(0.71f, 0.86f, 1.0f, 1.0f);
        bool flag_flashed = false;
        bool flag_bomb = false;
        bool flag_scoped = false;
        bool flag_reloading = false;
        bool flag_defusing = false;
        bool flag_money = false;
        bool flag_kit = false;
        bool flag_helmet = false;
        bool flag_nades = false;
        bool esp_rank = false;
        bool esp_3d_box = false;
        bool esp_oof = false;
        float esp_oof_radius = 280.f;
        float esp_oof_size = 14.f;
        ImVec4 esp_oof_color = ImVec4(1.f, 0.35f, 0.35f, 1.f);
        ImVec4 esp_3d_box_color = ImVec4(1.f, 0.45f, 0.2f, 0.95f);
        ImVec4 esp_rank_color = ImVec4(0.85f, 0.9f, 1.f, 1.f);
        bool sound_esp = false;
        float sound_esp_duration = 1.4f;
        float sound_esp_ring_size = 1.f;
        ImVec4 sound_esp_color = ImVec4(0.35f, 0.85f, 1.f, 0.95f);

        bool AnyEsp() const {
            return enabled &&
                   (esp || esp_skeleton || showHealth || showArmor || showNameTags || showWeapon || showWeaponIcon ||
                    showDistance || esp_rank || esp_3d_box || esp_oof || flag_flashed || flag_bomb || flag_scoped ||
                    flag_reloading || flag_defusing || flag_money || flag_kit || flag_helmet || flag_nades);
        }
    };

    extern PlayerVisualSettings enemy_visuals;
    extern PlayerVisualSettings friendly_visuals;
    extern bool local_visuals;
    extern bool local_glow;
    extern ImVec4 local_glow_color;

    struct WeaponSpreadSettings {
        bool enabled = false;
        bool followRecoil = false;
        float blendRadius = 50.f;
        ImVec4 center{.25f, .85f, 1.f, .08f};
        ImVec4 edge{.25f, .85f, 1.f, .35f};
    };

    extern WeaponSpreadSettings weapon_spread;

    inline bool WeaponSpreadEnabled() {
        return local_visuals && weapon_spread.enabled;
    }

    inline bool AnySoundEsp() {
        return (enemy_visuals.enabled && enemy_visuals.sound_esp) ||
               (friendly_visuals.enabled && friendly_visuals.sound_esp);
    }

    inline bool AnyPlayerGlow() {
        return (enemy_visuals.enabled && enemy_visuals.glow) || (friendly_visuals.enabled && friendly_visuals.glow) ||
               (local_visuals && local_glow);
    }

    extern bool glow_world_weapons;
    extern ImVec4 glow_world_weapon_color;
    extern bool glow_world_grenades;

    // Floating damage numbers (world, independent of hitmarker X)

    extern bool float_damage;
    extern float float_damage_duration;
    extern float float_damage_speed;
    // px/sec up
    extern ImVec4 float_damage_color;
    extern ImVec4 float_damage_head_color;
    extern ImVec4 float_damage_kill_color;
    // World ESP (dropped items / bomb / projectiles)

    extern bool world_esp_weapons;
    extern bool world_esp_weapon_icon;     // dropped weapon icon (vs text name)
    extern bool world_esp_weapon_distance; // distance under dropped weapon only
    extern bool world_esp_bomb;
    extern bool world_esp_bomb_timer;

    struct UtilityRangeStyle {
        ImVec4 outline, fill;
        float thickness = 2.f;
    };

    extern bool smoke_range, fire_range;
    extern UtilityRangeStyle smoke_range_style, fire_range_style;
    extern bool world_esp_smoke;
    extern bool world_esp_molotov;
    extern bool world_esp_he;
    extern bool world_esp_flash;
    extern bool world_esp_decoy;
    extern ImVec4 world_esp_weapon_color;
    extern ImVec4 world_esp_weapon_distance_color;
    extern ImVec4 world_esp_bomb_color;
    extern ImVec4 world_esp_smoke_color;
    extern ImVec4 world_esp_molotov_color;
    extern ImVec4 world_esp_he_color;
    extern ImVec4 world_esp_flash_color;
    extern ImVec4 world_esp_decoy_color;
    // Chams - mercey port (enemy: visible + XQZ, own material + colour each)
    extern bool enemyChams;          // visible layer
    extern bool enemyChamsInvisible; // XQZ layer
    extern int chamsMaterial;        // visible material (chams::ChamIds)
    extern int chamsMaterialXQZ;     // XQZ material
    extern ImVec4 colVisualChams;
    extern ImVec4 colVisualChamsIgnoreZ;
    extern bool enemyHitChams;
    extern bool enemyHistoryChams;
    extern int hitChamsMaterial;
    extern int historyChamsMaterial;
    extern ImVec4 hitChamsColor;
    extern ImVec4 historyChamsColor;
    extern bool teamChams;
    extern bool teamChamsInvisible;
    extern int teamChamsMaterial;
    extern int teamChamsMaterialXQZ;
    extern ImVec4 teamcolVisualChams;
    extern ImVec4 teamcolVisualChamsIgnoreZ;
    // Local body (thirdperson) + corpse
    extern bool localChams;
    extern int localChamsMaterial;
    extern ImVec4 colLocalChams;
    extern bool ragdollChams;
    extern int ragdollChamsMaterial;
    extern ImVec4 colRagdollChams;
    // Viewmodel: arms, gloves and weapon
    extern bool armChams;
    extern int armChamsMaterial;
    extern ImVec4 colArmChams;
    extern bool gloveChams;
    extern int gloveChamsMaterial;
    extern ImVec4 colGloveChams;
    extern bool viewmodelChams;
    extern int viewmodelChamsMaterial;
    extern ImVec4 colViewmodelChams;
    // World item chams (weapons/nades - mercey item groups)
    extern bool itemChams;
    extern bool itemChamsInvisible;
    extern int itemChamsMaterial;
    extern int itemChamsMaterialXQZ;
    extern ImVec4 colItemChams;
    extern ImVec4 colItemChamsIgnoreZ;
    extern bool itemChamsPistol;
    extern bool itemChamsSmg;
    extern bool itemChamsRifle;
    extern bool itemChamsShotgun;
    extern bool itemChamsSniper;
    extern bool itemChamsUtility;
    extern bool fovEnabled;
    extern float fov;
    // Aspect ratio (GetScreenAspectRatio / engine2 hook)

    extern bool aspect_ratio_enabled;
    extern float aspect_ratio;
    // width/height; e.g. 1.777... = 16:9
    // Viewmodel (CalcViewModel / GetViewModelOffsets hook)

    extern bool viewmodel_changer;
    extern float viewmodel_fov;
    extern float viewmodel_x;
    extern float viewmodel_y;
    extern float viewmodel_z;
    extern float viewmodel_yaw, viewmodel_pitch, viewmodel_roll;
    // Third person (OverrideView hook)

    extern bool thirdperson;
    extern float thirdperson_distance;
    extern float thirdperson_horizontal;
    extern float thirdperson_vertical;
    extern bool thirdperson_unlock_range;
    extern int thirdperson_key;
    extern int thirdperson_key_mode;
    // 0 Always, 1 Hold, 2 Toggle

    extern float antiflash_amount;
    // 0 = full flash, 100 = fully removed
    // Visuals -> Removals (early-
    extern bool remove_legs;
    // DRAWLEGS - firstperson legs
    extern bool remove_smoke;
    // DrawSmokeVertex + DrawSmokeArray
    extern bool remove_decals;
    // RENDERDECALS
    extern bool smoke_color;
    // t
    // tint C_SmokeGrenadeProjectile m_vSmokeColor
    extern ImVec4 smoke_color_value;
    extern bool fire_color;
    // t
    // tint fire particles (non-inferno) via ParticleDrawArray (particles.dll)

    extern ImVec4 fire_color_value;
    extern bool inferno_color;
    // t
    // separate molotov/inferno tint - Nade Pred tab (independent of Fire Color)

    extern ImVec4 inferno_color_value;
    extern bool explosion_color;
    // t
    // tint HE / explosion particles via ParticleDrawArray
    extern ImVec4 explosion_color_value;
    extern bool remove_crosshair;
    // DrawCrosshair - hide
    extern bool force_crosshair;
    // blocked
    // Visual recoil only - strip aim-punch from CViewSetup angles (OverrideView).
    // Does NOT zero GetRemovedAimPunch used by fire/seed/RCS (IDA 0x18088BBB0).
    extern bool remove_recoil;
    // Scope - custom lines (DrawScopeOverlay kill + ImGui redraw)

    extern bool scope_custom_lines;
    extern float scope_line_size;
    // 0..1 arm length (1 = full screen)

    extern float scope_line_gap;
    // center hole px
    extern float scope_line_thickness;
    // line width px (0.1 hairline .. 6)

    extern ImVec4 scope_line_color;
    // Scope zoom FOV (GetRenderFov + OverrideView) - per zoom stage
    extern bool scope_zoom_fov;
    extern float scope_fov_1;
    // first zoom (m_zoomLevel == 1)

    extern float scope_fov_2;
    // second zoom (m_zoomLevel >= 2)
    // Hide weapon + viewmodel arms while scoped
    extern bool scope_hide_viewmodel;
    // darkness 0..1 (0 = none, 1 = darkest)
    // Skybox t
    // tint via C_EnvSky::m_vTintColor (+ lighting-only) (+ lighting-only)

    extern bool skybox;
    extern ImVec4 skybox_color;
    extern bool sky_cloud_tint;
    extern ImVec4 sky_cloud_color;
    extern bool sky_sun_tint;
    extern ImVec4 sky_sun_color;
    // Global light (CGlobalLightBase::m_LightColor) via GLOBALLIGHTUPDATESTATE
    extern bool lighting;
    extern ImVec4 lighting_color;
    // World/map mesh t
    // tint via DrawArray (non-player meshes) (non-player meshes)

    extern bool map_color;
    extern ImVec4 map_color_value;
    extern bool props_color;
    extern ImVec4 props_color_value;
    // Custom gradient fog (SetupFog shader params)

    extern bool custom_fog;
    extern ImVec4 custom_fog_color;
    extern float custom_fog_start;
    extern float custom_fog_end;
    extern float custom_fog_falloff;
    // Weather via engine particles + rain_fx
    extern bool weather;
    extern int weather_mode;

    inline constexpr bool ValidWeatherMode(int mode) {
        return mode == 1 || mode == 3 || mode == 4;
    }

    extern float weather_intensity;
    // extra wait after fire ready (0 = ASAP)
    // Enemy spectate while dead (comp/MM - client force observer target)

    extern bool enemy_spectate;
    extern bool enemy_spectate_thirdperson;
    // chase vs in-eye
    // Bomb helpers
    extern bool auto_defuse;

    // AutofireMode
    // Autofire target priority (-style SortTargets)

    enum AfTargetSelect : int {
        AF_TARGET_CROSSHAIR = 0, // lowest FOV to aim po
        AF_TARGET_DISTANCE = 1,  // closest player origin
        AF_TARGET_DAMAGE = 2,    // highest estimated damage (+ lethal prefer)
        AF_TARGET_COUNT
    };

    // soft body bias; head still allowed
    // Multi-select hitboxes (shared index for aim / autofire / trigger lists)

    enum AimHitbox : int { HB_HEAD = 0, HB_NECK, HB_CHEST, HB_STOMACH, HB_PELVIS, HB_ARMS, HB_LEGS, HB_FEET, HB_COUNT };

    // 0 = center, 1 = full edge (MP on only)

    // 0 Always, 1 Hold, 2 Toggle
    struct LegitbotConfig {
        bool enabled = false, checks = true, visible = true, air = false, blinded = false;
        bool reload = false, switching = false, smoke = false, teammate = true;
        float vertical_fov = 5.f, horizontal_fov = 5.f;
        bool dynamic_fov = false, smoothing = true;
        int aim_mode = 1;
        float vertical_smooth = 10.f, horizontal_smooth = 10.f;
        bool dynamic_smooth = false;
        float aim_speed = 50.f;
        int aim_delay = 0, aim_duration = 0;
        bool aim_duration_unlock = false;
        int aim_damage_fallback = 0;
        bool target_lock = false;
        float lock_fov = 5.f;
        int switch_delay = 0, kill_delay = 0;
        std::array<bool, 8> hitgroups{true, false, false, false, false, false, false, false};
        std::array<int, 8> hitbox_order{0, 1, 2, 3, 4, 5, 6, 7};
        bool visible_hitbox = true, multipoint = false, per_hitbox = false, force_baim = false;
        float point_scale = 50.f;
        std::array<float, 8> point_scales{50.f, 50.f, 50.f, 50.f, 50.f, 50.f, 50.f, 50.f};
        int target_priority = 0, aim_min_damage = 0;
        float target_stickiness = 0.f;
        int curve = 0;
        float curve_strength = 0.f;
        bool random_strength = false;
        float acceleration = 0.f, deceleration = 0.f, speed_variation = 0.f;
        int reaction_delay = 0, overshoot_bullets = 0;
        float micro_correction = 0.f, aim_drift = 0.f, hand_tremor = 0.f;
        std::array<float, 8> custom_curve{0.f, 1.f / 7.f, 2.f / 7.f, 3.f / 7.f, 4.f / 7.f, 5.f / 7.f, 6.f / 7.f, 1.f};
        bool trigger = false;
        bool trigger_multipoint = false, trigger_per_hitbox = false;
        float trigger_point_scale = 51.f;
        std::array<float, 8> trigger_point_scales{51.f, 51.f, 51.f, 51.f, 51.f, 51.f, 51.f, 51.f};
        int trigger_delay = 0, first_shot_delay = 0, cooldown = 0;
        bool burst = false;
        int burst_shots = 0;
        std::array<bool, 8> trigger_hitgroups{true, true, true, true, true, false, false, false};
        bool velocity_check_enabled = true;
        float velocity_check = 0.f;
        float hitchance = 52.f;
        int hc_budget = 2;
        bool autostop = false, autoscope = false, air_autostop = false, seed_trigger = false;
        int min_damage = 0;
        bool scoped_only = false;
        std::array<bool, 8> trigger_checks{true, false, false, false, false, false, true, false};
        bool rcs = false, standalone_rcs = false;
        float rcs_x = 100.f, rcs_y = 100.f, rcs_smooth = 0.f;
        bool dynamic_rcs = false, reset_angle = false;
        float reset_smooth = 0.f;
        bool draw_path = false, draw_fov = false, draw_recoil_crosshair = false;

        bool AimMultipointEnabled() const {
            return multipoint || per_hitbox;
        }

        bool TriggerMultipointEnabled() const {
            return trigger_multipoint || trigger_per_hitbox;
        }

        float AimPointScale(int group = -1) const {
            const float value = group >= 0 && per_hitbox ? point_scales[group] : point_scale;
            return std::clamp(std::isfinite(value) ? value : 50.f, 0.f, 99.f);
        }

        float TriggerPointMinimum(int group = -1) const {
            if (group >= 0)
                return AimPointScale(group) + 1.f;
            float minimum = per_hitbox ? 1.f : AimPointScale() + 1.f;
            if (per_hitbox)
                for (int i = 0; i < 8; ++i)
                    minimum = (std::max)(minimum, AimPointScale(i) + 1.f);
            return minimum;
        }

        float TriggerPointScale(int group = -1) const {
            const bool individual = group >= 0 && trigger_per_hitbox;
            const float value = individual ? trigger_point_scales[group] : trigger_point_scale;
            return std::clamp(std::isfinite(value) ? value : 51.f, TriggerPointMinimum(individual ? group : -1), 100.f);
        }

        bool TriggerHitgroupRequired(int group) const {
            return hitgroups[group] || (force_baim && group >= 2 && group <= 4);
        }

        bool TriggerHitgroup(int group) const {
            return trigger_hitgroups[group] || TriggerHitgroupRequired(group);
        }

        void NormalizePointScales() {
            point_scale = std::clamp(std::isfinite(point_scale) ? point_scale : 50.f, 0.f, 99.f);
            for (auto& value : point_scales)
                value = std::clamp(std::isfinite(value) ? value : 50.f, 0.f, 99.f);
            trigger_point_scale = std::clamp(
                std::isfinite(trigger_point_scale) ? trigger_point_scale : 51.f, TriggerPointMinimum(), 100.f);
            for (int i = 0; i < 8; ++i)
                trigger_point_scales[i] =
                    std::clamp(std::isfinite(trigger_point_scales[i]) ? trigger_point_scales[i] : 51.f,
                               TriggerPointMinimum(i),
                               100.f);
        }

        bool VelocityCheckEnabled() const {
            return velocity_check_enabled && !autostop && !seed_trigger;
        }

        float RequiredHitchance() const {
            return seed_trigger ? 0.f : autostop && hitchance <= 0.f ? 52.f : hitchance;
        }
    };

    extern LegitbotConfig legitbot;

    struct MovementConfig {
        bool bhop = false, airstrafe = false, edgebug = false, edgejump = false;
        bool edgestop = false, fastladder = false, jumpbug = false, slowwalk = false;
        bool airstop = false, edgebug_jump_steps = false;
        bool quick_peek = false, duck_peek = false, peek_stop = true;
        bool peek_on_shot = true, peek_on_release = false, peek_filled = false;
        int peek_tolerance = 4;
        float peek_radius = 24.f, peek_glow = 1.f;
        ImVec4 peek_color{1.f, .85f, .15f, .65f};
        int edgebug_mode = 1, edgebug_passes = 1;
        float slowwalk_speed = 33.f;
        float turn_balance = 50.f;
        float turn_radius = 0.f;
        bool peek_dt_same_target = false; // Opt-in: only defer returning for a same-target DT follow-up.
    };

    extern MovementConfig movement;

    struct MeleeConfig {
        bool knife = false, knife_dt = false, zeus = false, zeus_lethal = true;
        int knife_attack = 0;
    };

    extern MeleeConfig melee;

    struct MovementFeature {
        const char* id;
        const char* label;
        bool* enabled;
    };

    inline std::array<MovementFeature, 11> MovementFeatures() {
        return {{{"bhop", "Bhop", &movement.bhop},
                 {"airstrafe", "Autostrafe", &movement.airstrafe},
                 {"airstop", "Standalone airstop when strafing", &movement.airstop},
                 {"edgebug", "Edgebug", &movement.edgebug},
                 {"edgejump", "Edgejump", &movement.edgejump},
                 {"edgestop", "Edgestop", &movement.edgestop},
                 {"fastladder", "Fastladder", &movement.fastladder},
                 {"jumpbug", "Jumpbug", &movement.jumpbug},
                 {"slowwalk", "Slowwalk", &movement.slowwalk},
                 {"quick_peek", "Quick Peek Assist", &movement.quick_peek},
                 {"duck_peek", "Duck Peek Assist", &movement.duck_peek}}};
    }

    // Rage-only shot log, fixed at the top left.
    extern bool visualise_aimbot;
    extern ImVec4 visualise_aimbot_color;
    extern bool hitlog;
    extern float hitlog_duration;
    extern int hitlog_max_rows;
    extern ImVec4 hitlog_color;
    extern ImVec4 hitlog_miss_color;
    // Bullet feedback copied from the mercey impacts feature.
    extern bool bullet_impact_effect;
    extern int bullet_impact_effect_type; // 0 overlay, 1 sparks, 2 both
    extern ImVec4 bullet_impact_effect_fill_color;
    extern ImVec4 bullet_impact_effect_edge_color;
    extern ImVec4 bullet_impact_effect_color_spark;
    extern float bullet_impact_effect_duration;
    extern bool bullet_impact_effect_glow;
    extern float bullet_impact_effect_glow_strength;
    extern bool bullet_tracers;
    extern ImVec4 bullet_tracer_color;
    extern float bullet_tracer_duration;
    extern float bullet_tracer_thickness;
    extern float bullet_tracer_glow_strength;
    // subtick_move + pred_upgrade: always on (hardcoded, no config)

    extern bool auto_accept;
    extern int auto_accept_delay;
    extern bool auto_accept_random;
    // TAB scoreboard weapon icons via panorama
    extern bool scoreboard_weapons;
    // Misc - mid-match loadout (P2000/USP, M4A4/M4A1-S): IsLoadoutAllowed + ignore server slot override
    extern bool unlock_inventory;

    struct WeaponSkin {

        int paint = 0;

        float wear = 0.f;

        int seed = 0;

        bool stattrak = false;
        int stattrak_count = 1337;

        char tag[64]{};
    };

    struct SkinLoadout {
        bool skin_knife = false;
        bool skin_glove = false;
        int skin_knife_def = 0;
        int skin_glove_def = 0;
        int skin_knife_paint = 0;
        int skin_glove_paint = 0;
        float skin_knife_wear = 0.f;
        float skin_glove_wear = 0.f;
        int skin_knife_seed = 0;
        int skin_glove_seed = 0;
        bool skin_knife_stattrak = false;
        bool skin_glove_stattrak = false;
        int skin_knife_stattrak_count = 1337;
        int skin_glove_stattrak_count = 1337;
        char skin_knife_tag[64]{};
        char skin_glove_tag[64]{};
        std::map<int, WeaponSkin> skin_weapons;
    };

    extern SkinLoadout skin_loadouts[2];

    inline SkinLoadout& SkinTeam(int team) {
        return skin_loadouts[team == 3 ? 1 : 0];
    }

    extern bool skin_agent;
    extern int skin_agent_t;
    extern int skin_agent_ct;

    // Custom paintkit colors (UC 754206 silvhook: g_vColor0..3 + g_vColorTint) - IDA 0x1807C7363 / 0x1807C7636
    extern bool custom_paint_enabled;
    extern ImVec4 custom_paint_color0;
    extern ImVec4 custom_paint_color1;
    extern ImVec4 custom_paint_color2;
    extern ImVec4 custom_paint_color3;
    extern bool custom_paint_glove_enabled;
    extern ImVec4 custom_paint_glove_color;

    // Thread-safe accessors for skin_weapons. The menu/Present thread mutates
    // the map (apply/disable/config Load/Save) while the game thread reads it
    // every FSN frame (SkinChanger Resolve / IsSkinnedWeaponId / HUD gate /
    // hkUnlockInventory). Direct std::map access from both sides is heap
    // corruption - ALL access must go through these.
    bool SkinWeapon_Find(int team, int def, WeaponSkin& out);
    bool SkinWeapon_Has(int team, int def);
    bool SkinWeapon_Empty(int team);
    void SkinWeapon_Set(int team, int def, const WeaponSkin& ws);
    void SkinWeapon_Erase(int team, int def);
    void SkinWeapon_Clear();
    // Whole-map copy under lock (config save iteration).
    std::map<int, WeaponSkin> SkinWeapon_Snapshot(int team);
    // Misc ? accept matchmaking popup via Lobby ReadyUp
    // Sound ESP (Visuals) ? ground walk rings on ESP-visible enemies

    // marker life sec

    // scale 0.5..3

    // Vote reveal /
    extern bool vote_reveal;
    // toast + chat who voted yes/no
    extern bool vote_auto;
    //
    // auto cast after delay
    extern int vote_auto_choice;
    // 0 Yes, 1 No
    extern float vote_auto_delay_ms;
    // Hitmarker (Misc) - COD screen X + world 3D at hit bone
    extern bool hitmarker;
    extern bool hitmarker_screen;
    extern bool hitmarker_world;
    extern bool hitmarker_show_damage;
    extern float hitmarker_size;
    // screen arm length px
    extern float hitmarker_thickness;
    extern float hitmarker_world_size;
    // world arm length px
    extern float hitmarker_duration;
    // life scale 0.25..2.5
    extern ImVec4 hitmarker_color;
    extern ImVec4 hitmarker_head_color;
    extern ImVec4 hitmarker_kill_color;
    // Hitsound (Misc) - names from the embedded Source 2 sound catalog
    extern bool hitsound;
    extern char hitsound_file[160];
    // normal hit basename
    extern char hitsound_head[160];
    // empty = use hitsound_file
    extern char hitsound_kill[160];
    // empty = use hitsound_file

    // Menu design (Config -> Design)

    extern ImVec4 menu_accent;
    extern ImVec4 menu_bg;
    extern ImVec4 menu_child_bg;
    extern ImVec4 menu_sidebar_bg;
    extern ImVec4 menu_border;
    extern ImVec4 menu_text;
    extern ImVec4 menu_text_muted;
    extern float menu_rounding;
    // 2..8
    extern float menu_opacity;
    extern bool menu_compact;
    extern float menu_glass;
    extern bool menu_widgets_follow;
    // tighter spacing
    extern int menu_dpi_scale;
    extern bool menu_dpi_custom;
    extern int menu_default_page;
    extern float menu_w;
    extern float menu_h;
    extern float menu_x;
    extern float menu_y;

    extern int menu_preset;
    // last applied preset index 0..7
    extern bool menu_sidebar_labels;
    // show tab names under icons
    // Overlay widgets (Misc)

    extern bool widget_keybinds;
    extern bool widget_bomb;
    extern bool widget_spectators;

    extern ImVec2 widget_keybinds_pos;
    // <0 =
    extern ImVec2 widget_bomb_pos;
    extern ImVec2 widget_spectators_pos;
    extern bool widget_keybinds_only_when_active;
    // hide until a bind is active
    extern bool widget_keybinds_show_all;
    // false = active binds only
    extern ImVec4 widget_keybinds_accent;
    extern ImVec4 widget_bomb_urgent;
    extern bool widget_bomb_show_damage;
    extern bool widget_bomb_show_defuse;
    extern ImVec4 widget_spectators_accent;
    extern bool widget_spectators_show_avatars;
    extern int widget_spectators_max;
    // 1..16
    extern bool remove_hud;
    extern bool remove_radar;
    extern bool remove_local_overhead;
    extern bool remove_friendly_overhead;
    extern bool remove_particles;
    extern bool remove_water;
    extern bool radar_reveal;
    // Grenade lineup helper ( 9xth style - stand circle + aim marker)

    extern bool grenade_helper;
    extern bool grenade_helper_only_held;
    // only show when holding matching nade
    extern float grenade_helper_stand_dist;
    // fade/draw stand circle
    extern float grenade_helper_aim_dist;
    // show aim marker when this close to stand
    extern float grenade_helper_select_dist;
    // nearest-lineup pick radius
    extern ImVec4 grenade_helper_color;
    extern ImVec4 grenade_helper_aim_color;
    // Capture keybind host (like aimbot/trigger) - edge-press captures
    extern bool grenade_helper_capture;
    extern int grenade_helper_capture_key;
    extern int grenade_helper_capture_key_mode;
    // 0 Always, 1 Hold, 2 Toggle
    extern int grenade_helper_capture_throw;
    // 0 Stand 1 Stand+Jump 2 Walk 3 Run 4 Crouch 5 Run+Jump
    extern int grenade_helper_capture_kind;
    // 0 Any ... 5 Decoy
    extern char grenade_helper_capture_name[64];
    // Live grenade trajectory preview (held weapon)
    extern bool nadepred_enable;
    extern bool nadepred_show_bounces;
    extern bool nadepred_in_air;
    extern bool nadepred_air_labels;
    extern ImVec4 nadepred_color;

    struct GrenadeReleaseConfig {
        bool enabled = false, protect_self = true, protect_team = true;
    };

    struct GrenadeControlConfig {
        bool straight_throw = false;
        std::array<GrenadeReleaseConfig, 3> release{}; // HE, flash, fire
        float he_damage = 40.f;
    };

    extern GrenadeControlConfig grenade_control;
    // Restore every field to config.cpp defaults (used before Load)

    void ResetToDefaults();
    // True while ConfigManager::Load mutates Config::* - World/Glow must skip
    // so ResetToDefaults cannot race FSN / Present.
    extern std::atomic<bool> loading;
    // Save configs as XPRESS_HUFF-compressed .json (LFAC magic header). Load
    // auto-detects both formats. Default ON;
    // toggle OFF if you want to hand-edit
    // the JSON on disk.
    extern bool config_compress;
    // UI font size - fixed built-in chain (segoeui.ttf -> arial.ttf).
    extern float ui_font_size;
    // 12..24 px, default 16
} // namespace Config
