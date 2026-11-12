// Ported from wubly/velocity @ 53ca95c120ac5ceb5e71622b5f5d0c3fff14fdcf.
#include "../velocity_sdk.h"
#include "../movement.hpp"
#include "../brake_math.h"

namespace Velocity::features::movement {

    bool slowwalk::on_create_move(systems::input::usercmd* cmd, float forcedTarget, float forcedStart) {
        if (forcedTarget < 0.f && !keybind.isActive(Config::movement.slowwalk)) {
            m_speed_compensation = 0.f;
            return false;
        }

        const auto local = systems::g_local.get();
        if (!local.pawn) {
            m_speed_compensation = 0.f;
            return false;
        }

        const auto& prestate = systems::g_prediction.pre();
        const bool in_air = !(prestate.flags & cstypes::entity_flags::on_ground);
        if (in_air && forcedTarget < 0.f) {
            m_speed_compensation = 0.f;
            return false;
        }

        const auto base = cmd->csgoUserCmd.pBaseCmd;
        const auto forward_move = base->flForwardMove;
        const auto side_move = base->flSideMove;

        if (forward_move == 0.0f && side_move == 0.0f && forcedTarget < 0.f) {
            m_speed_compensation = 0.f;
            return false;
        }

        const auto services = memory::read<std::uintptr_t>(local.pawn + Offsets::C_BasePlayerPawn_m_pMovementServices);
        const auto& weapon = CombatContext();
        const float maximum = weapon.valid
                                  ? weapon.weapon_max_speed
                                  : memory::read<float>(services + Offsets::CPlayer_MovementServices_m_flMaxspeed);
        const float target =
            forcedTarget >= 0.f ? forcedTarget : maximum * 0.34f * (Config::movement.slowwalk_speed * 0.01f);
        if (target < 0.f)
            return false;
        const float raw_yaw = base->pViewAngles->angValue.y;
        const float heading =
            (in_air || (forward_move == 0.f && side_move == 0.f)) && prestate.networked_velocity.length_2d() > 0.001f
                ? std::atan2(prestate.networked_velocity.y, prestate.networked_velocity.x)
                : raw_yaw * (std::numbers::pi_v<float> / 180.f) + std::atan2(side_move, forward_move);
        constexpr int count = 16;
        constexpr float interval = 1.f / count;
        forcedStart =
            forcedTarget >= 0.f && !in_air ? std::clamp(std::round(forcedStart * 64.f) / 64.f, 0.f, 63.f / 64.f) : 0.f;
        const float wish = AirWishSpeed(services);
        const float acceleration = (in_air ? Convar<float>("sv_airaccelerate") : Convar<float>("sv_accelerate")) *
                                   wish * prestate.surface_friction;
        const float wish_cap = in_air ? (std::min)(wish, Convar<float>("sv_air_max_wishspeed")) : wish;
        const float along =
            prestate.networked_velocity.x * std::cos(heading) + prestate.networked_velocity.y * std::sin(heading);
        const float across =
            -prestate.networked_velocity.x * std::sin(heading) + prestate.networked_velocity.y * std::cos(heading);
        const float tick_gain = acceleration * cstypes::tick_interval;
        // Native movement splits friction/acceleration around input samples.
        // Correct its measured steady error only after reaching cruise direction.
        if (!in_air && along > 0.f && std::abs(across) < target * 0.05f && std::abs(target - along) < tick_gain)
            m_speed_compensation = std::clamp(m_speed_compensation + (target - along) * 0.25f, -tick_gain, tick_gain);
        else
            m_speed_compensation = 0.f;
        // Combat limits are accuracy ceilings, not average cruise speeds.
        // Positive compensation can cross the movement-spread threshold even
        // when the pre-command velocity still appears accurate to HC.
        if (forcedTarget >= 0.f)
            m_speed_compensation = (std::min)(m_speed_compensation, 0.f);
        const math::vector3 goal{std::cos(heading) * (target + m_speed_compensation),
                                 std::sin(heading) * (target + m_speed_compensation),
                                 0.f};
        const float friction = in_air ? 0.f : Convar<float>("sv_friction") * prestate.surface_friction;
        const float stop = Convar<float>("sv_stopspeed");
        auto* closing = systems::g_input.acquire_subtick_step(base);
        if (!closing)
            return false;
        float yaw = raw_yaw, forward = prestate.last_movement_impulses.x, left = prestate.last_movement_impulses.y;
        math::vector3 velocity = prestate.networked_velocity;
        velocity.z = 0.f;
        auto step = [&](float when, float next_forward, float next_yaw) {
            auto* move = systems::g_input.acquire_subtick_step(base);
            if (!move)
                return false;
            float delta = next_yaw - yaw;
            math::helpers::normalize_angle(delta);
            SetWhen(move, when);
            SetButton(move, 0);
            SetPressed(move, false);
            SetForwardDelta(move, next_forward - forward);
            SetLeftDelta(move, -left);
            SetPitchDelta(move, 0.f);
            SetYawDelta(move, delta);
            forward = next_forward;
            left = 0.f;
            yaw = next_yaw;
            return true;
        };
        // Quantized movement rounds fractional axes away. Full forward pulses
        // regulate speed in subticks; only their movement yaw changes temporarily.
        const float inputLength = (std::min)(1.f, std::hypot(forward_move, side_move));
        const float inputHeading = raw_yaw * (std::numbers::pi_v<float> / 180.f) + std::atan2(side_move, forward_move);
        const math::vector3 inputDirection{std::cos(inputHeading), std::sin(inputHeading), 0.f};
        auto part = [&](float when, float until, bool applyBrake) {
            const float span = until - when, dt = span * cstypes::tick_interval;
            const float speed = velocity.length_2d();
            if (speed > 0.f)
                velocity = velocity * (std::max)(0.f, 1.f - (std::max)(stop, speed) * friction * dt / speed);
            if (!applyBrake) {
                const float along = velocity.x * inputDirection.x + velocity.y * inputDirection.y;
                const float gain =
                    (std::min)(acceleration * inputLength * dt, (std::max)(0.f, wish_cap * inputLength - along));
                if (!step(when, inputLength, inputHeading * (180.f / std::numbers::pi_v<float>)))
                    return false;
                if (inputLength > 0.f)
                    velocity = velocity + inputDirection * gain;
                return true;
            }
            const int quanta = std::clamp(int(std::lround(span * 64.f)), 1, 4);
            const auto pulse = MovementBrake::Solve(
                velocity.x, velocity.y, goal.x, goal.y, acceleration, wish_cap, dt, forcedTarget >= 0.f, quanta);
            const math::vector3 direction{pulse.x, pulse.y, 0.f};
            const float gain = pulse.gain, duty = pulse.duty;
            const float next_yaw = std::atan2(direction.y, direction.x) * (180.f / std::numbers::pi_v<float>);
            if (!step(when, duty > 0.f ? 1.f : 0.f, next_yaw))
                return false;
            velocity = velocity + direction * (gain * duty);
            return !(duty > 0.f && duty < 1.f) || step(when + span * duty, 0.f, next_yaw);
        };
        for (int i = 0; i < count; ++i) {
            const float begin = i * interval, end = (i + 1) * interval;
            if (forcedStart > begin && forcedStart < end) {
                if (!part(begin, forcedStart, false) || !part(forcedStart, end, true))
                    break;
            } else if (!part(begin, end, begin >= forcedStart))
                break;
        }
        float delta = raw_yaw - yaw;
        math::helpers::normalize_angle(delta);
        SetWhen(closing, std::nextafter(1.f, 0.f));
        SetButton(closing, 0);
        SetPressed(closing, false);
        SetForwardDelta(closing, -forward);
        SetLeftDelta(closing, -left);
        SetPitchDelta(closing, 0.f);
        SetYawDelta(closing, delta);
        cmd->nButtons.nValue &= ~cstypes::command_buttons::in_sprint;
        cmd->nButtons.nValueChanged &= ~cstypes::command_buttons::in_sprint;
        cmd->nButtons.nValueScroll &= ~cstypes::command_buttons::in_sprint;
        SetForward(base, 0.f);
        SetLeft(base, 0.f);
        return true;
    }

} // namespace Velocity::features::movement
