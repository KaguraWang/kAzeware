// Ported from wubly/velocity @ 53ca95c120ac5ceb5e71622b5f5d0c3fff14fdcf.
#include "../velocity_sdk.h"
#include "../movement.hpp"

namespace Velocity::features::movement {

    void edgestop::on_create_move(systems::input::usercmd* cmd) const {
        if (!keybind.isActive(Config::movement.edgestop) || keybind.isActive(Config::movement.edgejump)) {
            return;
        }

        const auto local = systems::g_local.get();
        if (!local.pawn) {
            return;
        }

        const auto& prestate = systems::g_prediction.pre();
        if (!(prestate.flags & cstypes::entity_flags::on_ground)) {
            return;
        }

        const auto velocity = prestate.networked_velocity;
        const auto speed = velocity.length_2d();

        const auto move_type = memory::read<std::uint8_t>(local.pawn + Offsets::C_BaseEntity_m_nActualMoveType);
        if (move_type == cstypes::move_type::ladder || move_type == cstypes::move_type::noclip) {
            return;
        }

        const auto game_scene_node = memory::read<std::uintptr_t>(local.pawn + Offsets::C_BaseEntity_m_pGameSceneNode);
        if (!game_scene_node) {
            return;
        }

        const auto origin = memory::read<math::vector3>(game_scene_node + Offsets::CGameSceneNode_m_vecAbsOrigin);
        const auto movement_services =
            memory::read<std::uintptr_t>(local.pawn + Offsets::C_BasePlayerPawn_m_pMovementServices);

        if (!movement_services) {
            return;
        }

        const auto collision = local.pawn + Offsets::C_BaseModelEntity_m_Collision;
        const auto mins = memory::read<math::vector3>(collision + Offsets::CCollisionProperty_m_vecMins);
        const auto maxs = memory::read<math::vector3>(collision + Offsets::CCollisionProperty_m_vecMaxs);

        auto trace_mask{0ull};
        {
            const auto pawn_ptr = memory::read<std::uintptr_t>(movement_services + 56);
            trace_mask = memory::read<std::uintptr_t>(pawn_ptr + 0xd50);

            if (!pawn_ptr || (memory::read<std::uint32_t>(pawn_ptr + 0x3f4) & 0x10)) {
                trace_mask |= 0x20;
            }
        }

        const auto filter = systems::g_tracing.make_player_movement_filter(local.pawn, trace_mask, 11);
        const auto sv_standable_normal = Convar<float>("sv_standable_normal");

        const auto check_edge = [&](int ticks_ahead) -> bool {
            auto predicted = origin;
            predicted.x += velocity.x * cstypes::tick_interval * static_cast<float>(ticks_ahead);
            predicted.y += velocity.y * cstypes::tick_interval * static_cast<float>(ticks_ahead);

            const auto start = math::vector3{predicted.x, predicted.y, predicted.z + 2.0f};
            const auto end = math::vector3{predicted.x, predicted.y, predicted.z - 4.0f};

            const auto result =
                systems::g_tracing.trace_player_bbox(start, end, {mins, maxs}, filter, movement_services);
            return result.fraction >= 1.0f || result.normal.z < sv_standable_normal;
        };

        const auto at_edge = check_edge(1);

        auto approaching_edge{false};

        if (!at_edge && speed > 1.0f) {
            const auto sv_friction = Convar<float>("sv_friction");
            const auto sv_stopspeed = Convar<float>("sv_stopspeed");
            const auto ticks_needed = std::max(
                2,
                static_cast<int>(std::ceilf(speed / (sv_friction * prestate.surface_friction *
                                                     std::fmaxf(speed, sv_stopspeed) * cstypes::tick_interval))));

            for (auto i = 2; i <= ticks_needed + 2; ++i) {
                if (check_edge(i)) {
                    approaching_edge = true;
                    break;
                }
            }
        }

        if (!at_edge && !approaching_edge) {
            return;
        }

        const auto base = cmd->csgoUserCmd.pBaseCmd;
        if (!base) {
            return;
        }

        const auto view_yaw = base->pViewAngles->angValue.y;
        const auto view_yaw_rad = view_yaw * (std::numbers::pi_v<float> / 180.0f);
        const auto max_weapon_speed = CombatContext().valid ? CombatContext().weapon_max_speed : 250.0f;

        if (at_edge) {
            const auto forward_move = base->flForwardMove;
            const auto left_move = base->flSideMove;

            const auto wish_x = std::cosf(view_yaw_rad) * forward_move - std::sinf(view_yaw_rad) * left_move;
            const auto wish_y = std::sinf(view_yaw_rad) * forward_move + std::cosf(view_yaw_rad) * left_move;
            const auto wish_len = std::sqrtf(wish_x * wish_x + wish_y * wish_y);

            if (wish_len > 0.001f) {
                const auto wish_norm_x = wish_x / wish_len;
                const auto wish_norm_y = wish_y / wish_len;

                auto test_origin = origin;
                test_origin.x += wish_norm_x * 4.0f;
                test_origin.y += wish_norm_y * 4.0f;

                const auto start = math::vector3{test_origin.x, test_origin.y, origin.z + 2.0f};
                const auto end = math::vector3{test_origin.x, test_origin.y, origin.z - 4.0f};

                const auto result =
                    systems::g_tracing.trace_player_bbox(start, end, {mins, maxs}, filter, movement_services);
                const auto input_toward_edge = result.fraction >= 1.0f || result.normal.z < sv_standable_normal;

                if (input_toward_edge) {
                    const auto pushback_yaw =
                        std::atan2f(wish_norm_y, wish_norm_x) * (180.0f / std::numbers::pi_v<float>)+180.0f;
                    const auto pushback_rotation = (view_yaw - pushback_yaw) * (std::numbers::pi_v<float> / 180.0f);

                    SetForward(base, std::clamp(std::cosf(pushback_rotation) * 0.3f, -1.0f, 1.0f));
                    SetLeft(base, std::clamp(std::sinf(pushback_rotation) * -0.3f, -1.0f, 1.0f));

                    auto buttons = cmd->nButtons.nValue;
                    buttons &= ~static_cast<std::uintptr_t>(
                        cstypes::command_buttons::in_forward | cstypes::command_buttons::in_back |
                        cstypes::command_buttons::in_moveleft | cstypes::command_buttons::in_moveright);

                    if (base->flForwardMove > 0.0f) {
                        buttons |= cstypes::command_buttons::in_forward;
                    } else if (base->flForwardMove < 0.0f) {
                        buttons |= cstypes::command_buttons::in_back;
                    }

                    if (base->flSideMove > 0.0f) {
                        buttons |= cstypes::command_buttons::in_moveleft;
                    } else if (base->flSideMove < 0.0f) {
                        buttons |= cstypes::command_buttons::in_moveright;
                    }

                    cmd->nButtons.nValue = buttons;

                    if (speed <= 5.0f) {
                        return;
                    }
                }
            }

            if (speed > 1.0f) {
                const auto stop_yaw = std::atan2f(velocity.y, velocity.x) * (180.0f / std::numbers::pi_v<float>)+180.0f;
                const auto rotation = (view_yaw - stop_yaw) * (std::numbers::pi_v<float> / 180.0f);

                const auto sv_accelerate = Convar<float>("sv_accelerate");
                const auto accel_speed =
                    sv_accelerate * max_weapon_speed * prestate.surface_friction * cstypes::tick_interval;
                const auto move_speed = (speed < accel_speed) ? speed : max_weapon_speed;

                const auto fwd = std::clamp(move_speed / max_weapon_speed, 0.0f, 1.0f);

                SetForward(base, std::clamp(std::cosf(rotation) * fwd, -1.0f, 1.0f));
                SetLeft(base, std::clamp(std::sinf(rotation) * fwd * -1.0f, -1.0f, 1.0f));

                auto buttons = cmd->nButtons.nValue;
                buttons &= ~static_cast<std::uintptr_t>(
                    cstypes::command_buttons::in_forward | cstypes::command_buttons::in_back |
                    cstypes::command_buttons::in_moveleft | cstypes::command_buttons::in_moveright);

                if (base->flForwardMove > 0.0f) {
                    buttons |= cstypes::command_buttons::in_forward;
                } else if (base->flForwardMove < 0.0f) {
                    buttons |= cstypes::command_buttons::in_back;
                }

                if (base->flSideMove > 0.0f) {
                    buttons |= cstypes::command_buttons::in_moveleft;
                } else if (base->flSideMove < 0.0f) {
                    buttons |= cstypes::command_buttons::in_moveright;
                }

                cmd->nButtons.nValue = buttons;
            }

            return;
        }

        const auto stop_yaw = std::atan2f(velocity.y, velocity.x) * (180.0f / std::numbers::pi_v<float>)+180.0f;
        const auto rotation = (view_yaw - stop_yaw) * (std::numbers::pi_v<float> / 180.0f);

        const auto sv_accelerate = Convar<float>("sv_accelerate");
        const auto accel_speed = sv_accelerate * max_weapon_speed * prestate.surface_friction * cstypes::tick_interval;
        const auto move_speed = (speed < accel_speed) ? speed : max_weapon_speed;

        const auto fwd = std::clamp(move_speed / max_weapon_speed, 0.0f, 1.0f);

        SetForward(base, std::clamp(std::cosf(rotation) * fwd, -1.0f, 1.0f));
        SetLeft(base, std::clamp(std::sinf(rotation) * fwd * -1.0f, -1.0f, 1.0f));

        auto buttons = cmd->nButtons.nValue;
        buttons &= ~static_cast<std::uintptr_t>(
            cstypes::command_buttons::in_forward | cstypes::command_buttons::in_back |
            cstypes::command_buttons::in_moveleft | cstypes::command_buttons::in_moveright);

        if (base->flForwardMove > 0.0f) {
            buttons |= cstypes::command_buttons::in_forward;
        } else if (base->flForwardMove < 0.0f) {
            buttons |= cstypes::command_buttons::in_back;
        }

        if (base->flSideMove > 0.0f) {
            buttons |= cstypes::command_buttons::in_moveleft;
        } else if (base->flSideMove < 0.0f) {
            buttons |= cstypes::command_buttons::in_moveright;
        }

        cmd->nButtons.nValue = buttons;
    }

} // namespace Velocity::features::movement
