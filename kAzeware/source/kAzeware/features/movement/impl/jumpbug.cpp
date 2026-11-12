// Ported from wubly/velocity @ 53ca95c120ac5ceb5e71622b5f5d0c3fff14fdcf.
#include "../velocity_sdk.h"
#include "../movement.hpp"

namespace Velocity::features::movement {

    void jumpbug::on_create_move(systems::input::usercmd* cmd) {
        this->m_active_this_tick = false;

        if (!keybind.isActive(Config::movement.jumpbug)) {
            m_prepared = false;
            return;
        }

        try_landing_jump(cmd);
    }

    void jumpbug::try_landing_jump(systems::input::usercmd* cmd) {
        if (g_edgebug.active_this_tick())
            return;
        auto& buttons = cmd->nButtons;
        const auto local = systems::g_local.get();
        auto* base = cmd->csgoUserCmd.pBaseCmd;
        if (!local.pawn || !base || !(buttons.nValue & cstypes::command_buttons::in_jump)) {
            m_prepared = false;
            return;
        }
        const auto move = memory::read<std::uint8_t>(local.pawn + Offsets::C_BaseEntity_m_nActualMoveType);
        const auto& pre = systems::g_prediction.pre();
        if (move == cstypes::move_type::ladder || move == cstypes::move_type::noclip ||
            (pre.flags & cstypes::entity_flags::on_ground) || pre.networked_velocity.z >= 0.f) {
            m_prepared = false;
            return;
        }
        const auto services = memory::read<std::uintptr_t>(local.pawn + Offsets::C_BasePlayerPawn_m_pMovementServices);
        if (!services)
            return;
        auto maxs = memory::read<math::vector3>(local.pawn + Offsets::C_BaseModelEntity_m_Collision +
                                                Offsets::CCollisionProperty_m_vecMaxs);
        const math::vector3 mins{-maxs.x, -maxs.y, 0.f};
        const auto filter = systems::g_tracing.make_player_movement_filter(
            local.pawn, memory::read<std::uintptr_t>(local.pawn + 0xd50), 11);
        constexpr float dt = cstypes::tick_interval, horizon = dt * 4.f;
        auto velocity = pre.networked_velocity;
        velocity.z -= memory::read<float>(local.pawn + Offsets::C_BaseEntity_m_flGravityScale) *
                      Convar<float>("sv_gravity") * horizon * .5f;
        const math::vector3 start = pre.networked_origin;
        math::vector3 end = start + velocity * horizon;
        // Prepare four ticks before the 9-11u release window, rather than
        // four ticks before contact. Convert the expanded trace back to distance.
        end.z -= 11.f;
        const bool prepared = m_prepared;
        const bool crouched = memory::read<bool>(services + Offsets::CCSPlayer_MovementServices_m_bDucked) &&
                              memory::read<float>(services + Offsets::CCSPlayer_MovementServices_m_flDuckAmount) >= 1.f;
        const auto step = [&](std::uint64_t button, bool pressed, float time) {
            if (auto* s = systems::g_input.acquire_subtick_step(base)) {
                SetButton(s, button);
                SetPressed(s, pressed);
                SetWhen(s, time);
                SetForwardDelta(s, 0.f);
                SetLeftDelta(s, 0.f);
            }
        };
        const auto holdDuck = [&](bool claimJump = true) {
            buttons.nValue |= cstypes::command_buttons::in_duck;
            if (prepared)
                buttons.nValueChanged &= ~cstypes::command_buttons::in_duck;
            else
                buttons.nValueChanged |= cstypes::command_buttons::in_duck;
            buttons.nValueScroll &= ~cstypes::command_buttons::in_duck;
            if (!prepared)
                step(cstypes::command_buttons::in_duck, true, 0.f);
            if (!claimJump)
                return;
            buttons.nValue &= ~cstypes::command_buttons::in_jump;
            buttons.nValueChanged &= ~cstypes::command_buttons::in_jump;
            buttons.nValueScroll &= ~cstypes::command_buttons::in_jump;
            step(cstypes::command_buttons::in_jump, false, 0.f);
            m_active_this_tick = true;
        };
        const auto cancelPreparation = [&] {
            if (prepared && !(buttons.nValue & cstypes::command_buttons::in_duck))
                step(cstypes::command_buttons::in_duck, false, 0.f);
            m_prepared = false;
        };
        const auto hit = systems::g_tracing.trace_player_bbox(start, end, {mins, maxs}, filter, services);
        if (hit.fraction <= 0.f || hit.fraction >= 1.f || hit.normal.z < .985f) {
            // Keep an existing crouch when no Jumpbug surface qualifies.
            // Bhop can still recognize a standable surface with its own trace.
            if (prepared && crouched)
                holdDuck(false);
            else
                cancelPreparation();
            return;
        }
        const math::vector3 trace_delta = end - start;
        const float distance = -trace_delta.dot(hit.normal) * hit.fraction / hit.normal.z;
        const float descent = -pre.networked_velocity.dot(hit.normal) / hit.normal.z;
        const float gravity =
            memory::read<float>(local.pawn + Offsets::C_BaseEntity_m_flGravityScale) * Convar<float>("sv_gravity");
        const float displacement = descent * dt + .5f * gravity * dt * dt;
        // A sent duck edge does not guarantee a crouched movement hull.
        // When the release window arrives without one, leave jump available
        // for Bhop to schedule against ordinary ground contact.
        if (!crouched && distance <= 11.f + displacement) {
            cancelPreparation();
            return;
        }
        if (distance < 9.f) {
            // The uncrouch window is gone. Preserve the crouched hull and
            // let Bhop time the ordinary landing in this command.
            holdDuck(false);
            return;
        }
        holdDuck();
        if (!prepared || distance > 11.f + displacement) {
            m_prepared = true;
            return;
        }
        // Uncrouch in the 9-11 unit crouchbug window, before normal floor impact.
        // Native jump timing is quantized to 1/64 of a tick. Rounding a
        // release near 1.0 into the next tick discards it and misses the window.
        const float contact = drop_fraction(distance - 11.f, descent, gravity);
        const float halfWindow = Convar<float>("sv_bhop_time_window") * .5f / dt;
        const float first = std::ceil(contact * 64.f) / 64.f;
        const float last =
            std::floor(
                (std::min)({63.f / 64.f, contact + halfWindow, drop_fraction(distance - 9.f, descent, gravity)}) *
                64.f) /
            64.f;
        if (first > last)
            return;
        // Release at the first valid contact. A late release gives the next
        // movement slice less room to apply the duck and jump edges together.
        const auto press = g_jump_press_window.reserve(cmd, first, last);
        if (!press)
            return;
        const float when = *press;
        m_landing_fraction = when;
        m_prepared = false;
        // The command ends uncrouched, matching its explicit release below.
        buttons.nValue &= ~cstypes::command_buttons::in_duck;
        buttons.nValueChanged &= ~cstypes::command_buttons::in_duck;
        buttons.nValueScroll &= ~cstypes::command_buttons::in_duck;
        step(cstypes::command_buttons::in_duck, false, when);
        step(cstypes::command_buttons::in_jump, true, when);
        if (when + 1.f / 64.f < 1.f)
            step(cstypes::command_buttons::in_jump, false, when + 1.f / 64.f);
    }

    float jumpbug::get_impulse_mul(std::uintptr_t local_pawn) const {
        const auto movement_services =
            memory::read<std::uintptr_t>(local_pawn + Offsets::C_BasePlayerPawn_m_pMovementServices);
        if (!movement_services) {
            return 0.0f;
        }

        const auto stamina = memory::read<float>(movement_services + Offsets::CCSPlayer_MovementServices_m_flStamina);

        if (Convar<bool>("sv_legacy_jump")) {
            if (stamina <= 0.0f) {
                return 1.0f;
            }

            return std::clamp(1.0f - (stamina / 100.0f), 0.0f, 1.0f);
        }

        const auto current_tick = memory::read<int>(memory::read<std::uintptr_t>(GlobalVarsAddress()) + 0x44);
        const auto modern_jump = movement_services + Offsets::CCSPlayer_MovementServices_m_ModernJump;
        const auto landing_vel_z =
            memory::read<float>(modern_jump + Offsets::CCSPlayerModernJump_m_flLastLandedVelocityZ);
        const auto landed_tick =
            memory::read<std::uint32_t>(modern_jump + Offsets::CCSPlayerModernJump_m_nLastLandedTick);
        const auto base = std::clamp((landing_vel_z * 0.0005f) + 1.0f, 0.02f, 1.0f);
        const auto ticks_since_landing = static_cast<float>(current_tick - landed_tick);

        auto result = std::clamp(base + (ticks_since_landing * 0.6f), 0.0f, 1.0f);

        if (stamina > 0.0f) {
            result *= std::clamp(1.0f - (stamina / 100.0f), 0.0f, 1.0f);
        }

        return result;
    }

} // namespace Velocity::features::movement
