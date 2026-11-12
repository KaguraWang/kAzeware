// Ported from wubly/velocity @ 53ca95c120ac5ceb5e71622b5f5d0c3fff14fdcf.
#include "../velocity_sdk.h"
#include "../movement.hpp"

namespace Velocity::features::movement {

    std::optional<float> jump_press_window::reserve(systems::input::usercmd* cmd, float when, float latest) {
        if (m_command == cmd->nCommandNumber)
            return m_reserved;
        if (cmd->nCommandNumber < m_command)
            m_last_press = -1e30;
        m_command = cmd->nCommandNumber;
        m_reserved.reset();
        // One newly simulated command is one movement tick. nClientTick can
        // remain unchanged across several commands during network correction.
        const double cooldown = Convar<float>("sv_jump_spam_penalty_time") / cstypes::tick_interval;
        const double ready = m_last_press + cooldown - double(cmd->nCommandNumber);
        // A press late in the previous command can leave only its first part
        // on cooldown. Use the next native time quantum instead of waiting a
        // whole grounded tick and applying floor friction before the next jump.
        if (when <= ready)
            when = static_cast<float>((std::floor(ready * 64.) + 1.) / 64.);
        if (when > latest)
            return std::nullopt;
        m_last_press = double(cmd->nCommandNumber) + when;
        m_reserved = when;
        return m_reserved;
    }

    namespace {

        constexpr float jump_quantum = 1.f / 64.f;

        struct landing_window {
            float contact;
            float latest;
        };

        [[nodiscard]] std::optional<landing_window> predict_landing_window(std::uintptr_t local_pawn,
                                                                           std::uintptr_t movement_services,
                                                                           const systems::prediction::state& prestate) {
            if (prestate.networked_velocity.z > 0.0f) {
                return std::nullopt;
            }

            const auto maxs = memory::read<math::vector3>(local_pawn + Offsets::C_BaseModelEntity_m_Collision +
                                                          Offsets::CCollisionProperty_m_vecMaxs);
            // Client collision mins can be transiently offset during prediction/rendering.
            // Movement uses a symmetric hull rooted at the player's feet.
            const math::vector3 mins{-maxs.x, -maxs.y, 0.f};

            // The current predicted origin already includes duck transitions.
            // Expanding/downshifting it again predicts contact before the feet land.
            const auto trace_origin = prestate.networked_origin;

            auto trace_mask{0ull};
            {
                const auto pawn_ptr = memory::read<std::uintptr_t>(movement_services + 56);
                trace_mask = memory::read<std::uintptr_t>(pawn_ptr + 0xd50);

                if (!pawn_ptr || (memory::read<std::uint32_t>(pawn_ptr + 0x3f4) & 0x10)) {
                    trace_mask |= 0x20;
                }
            }

            const auto filter = systems::g_tracing.make_player_movement_filter(local_pawn, trace_mask, 11);
            const auto sv_gravity = Convar<float>("sv_gravity");
            const auto sv_standable_normal = Convar<float>("sv_standable_normal");
            const auto gravity_scale = memory::read<float>(local_pawn + Offsets::C_BaseEntity_m_flGravityScale);
            const float halfWindow = Convar<float>("sv_bhop_time_window") * .5f / cstypes::tick_interval;
            constexpr float horizon = cstypes::tick_interval;

            auto velocity = prestate.networked_velocity;
            velocity.z -= (gravity_scale * sv_gravity * horizon) * 0.5f;

            const math::vector3 trace_start = trace_origin;
            math::vector3 trace_end{};

            trace_end.x = trace_origin.x + velocity.x * horizon;
            trace_end.y = trace_origin.y + velocity.y * horizon;
            trace_end.z = trace_origin.z + velocity.z * horizon;
            const float displacement = trace_start.z - trace_end.z;
            if (displacement <= 0.f)
                return std::nullopt;
            trace_end.z -= 2.f;

            const auto result =
                systems::g_tracing.trace_player_bbox(trace_start, trace_end, {mins, maxs}, filter, movement_services);
            if (result.fraction <= 0.0f || result.fraction >= 1.0f || result.normal.z <= 0.f ||
                result.normal.z < sv_standable_normal) {
                return std::nullopt;
            }

            const math::vector3 trace_delta = trace_end - trace_start;
            const float distance = -trace_delta.dot(result.normal) * result.fraction / result.normal.z;
            const float descent = -prestate.networked_velocity.dot(result.normal) / result.normal.z;
            const float gravity = gravity_scale * sv_gravity;
            const float contact = drop_fraction(distance - 2.f, descent, gravity);
            // A press can be discarded at the next command's landing, while
            // still leaving that command on the native jump-spam cooldown.
            if (contact >= 1.f)
                return std::nullopt;
            const float latest = std::floor((std::min)(63.f / 64.f, contact + halfWindow) * 64.f) / 64.f;
            return landing_window{contact, latest};
        }

        void apply_landing_jump(proto::base_usercmd_pb* base, float when) {
            const auto subtick_moves = base;
            const auto release_when = 0.0f;

            if (release_when < when) {
                if (const auto jump_up = systems::g_input.acquire_subtick_step(subtick_moves)) {
                    SetButton(jump_up, cstypes::command_buttons::in_jump);
                    SetPressed(jump_up, false);
                    SetWhen(jump_up, release_when);
                    SetForwardDelta(jump_up, 0.0f);
                    SetLeftDelta(jump_up, 0.0f);
                }
            }

            if (const auto jump_down = systems::g_input.acquire_subtick_step(subtick_moves)) {
                SetButton(jump_down, cstypes::command_buttons::in_jump);
                SetPressed(jump_down, true);
                SetWhen(jump_down, when);
                SetForwardDelta(jump_down, 0.0f);
                SetLeftDelta(jump_down, 0.0f);
            }
            const float end = when + jump_quantum;
            if (end < 1.f)
                if (auto* jump_up = systems::g_input.acquire_subtick_step(subtick_moves)) {
                    SetButton(jump_up, cstypes::command_buttons::in_jump);
                    SetPressed(jump_up, false);
                    SetWhen(jump_up, end);
                    SetForwardDelta(jump_up, 0.f);
                    SetLeftDelta(jump_up, 0.f);
                }
        }

    } // namespace

    void bhop::on_create_move(systems::input::usercmd* cmd) const {
        if (!keybind.isActive(Config::movement.bhop)) {
            return;
        }

        if (Convar<bool>("sv_autobunnyhopping")) {
            return;
        }

        if (!(cmd->nButtons.nValue & cstypes::command_buttons::in_jump)) {
            return;
        }

        if (features::movement::g_jumpbug.active_this_tick()) {
            return;
        }

        const auto local = systems::g_local.get();
        if (!local.pawn) {
            return;
        }

        const auto move_type = memory::read<std::uint8_t>(local.pawn + Offsets::C_BaseEntity_m_nActualMoveType);
        if (move_type == cstypes::move_type::ladder || move_type == cstypes::move_type::noclip) {
            return;
        }

        const auto& prestate = systems::g_prediction.pre();
        const bool onGround = (prestate.flags & cstypes::entity_flags::on_ground) != 0;
        // The command starts with jump released. A fresh subtick press works even
        // when a preceding jumpbug command ended with jump held.
        cmd->nButtons.nValue &= ~cstypes::command_buttons::in_jump;
        cmd->nButtons.nValueChanged &= ~cstypes::command_buttons::in_jump;
        cmd->nButtons.nValueScroll &= ~cstypes::command_buttons::in_jump;
        if (onGround) {
            if (auto* base = cmd->csgoUserCmd.pBaseCmd)
                // Base buttons describe the final state, not a timed press.
                // Supply the edge even when the pawn is already grounded.
                if (auto press = g_jump_press_window.reserve(cmd, 1.f / 64.f))
                    apply_landing_jump(base, *press);
            return;
        }

        const auto movement_services =
            memory::read<std::uintptr_t>(local.pawn + Offsets::C_BasePlayerPawn_m_pMovementServices);
        if (!movement_services) {
            return;
        }

        const auto landing = predict_landing_window(local.pawn, movement_services, prestate);
        if (!landing) {
            return;
        }

        const auto base = cmd->csgoUserCmd.pBaseCmd;
        if (!base) {
            return;
        }

        // CheckJump runs before the movement slice updates ground state.
        // Pick a later native quantum than the slice that can establish contact.
        const float first = (std::max)(jump_quantum, std::ceil(landing->contact * 64.f) * jump_quantum);
        float checkpoint = 1.f;
        for (int i = 0; i < base->subtickMovesField.nCurrentSize; ++i) {
            const auto* step = StepAt(base, i);
            if (!step || step->nButton || !(step->nCachedBits & MOVESTEP_BITS_WHEN) || !std::isfinite(step->flWhen) ||
                step->flWhen <= 0.f)
                continue;
            const float phase = std::round(step->flWhen * 64.f) * jump_quantum;
            if (phase >= first)
                checkpoint = (std::min)(checkpoint, phase);
        }
        // Sparse/idle movement may not supply a timely boundary. Split the
        // existing physical command at contact, without changing its axes.
        float latest = (std::min)(62.f / 64.f, landing->latest);
        bool insertCheckpoint = checkpoint + jump_quantum > latest;
        if (insertCheckpoint)
            checkpoint = first;
        else if (checkpoint == 60.f / 64.f && landing->latest == 63.f / 64.f) {
            // Ground may only be established during the penultimate quantum.
            // Leave that quantum released, then press in the last one. There
            // is no later in-command slice to repeat the edge and the base
            // buttons already end released.
            checkpoint = 62.f / 64.f;
            latest = 63.f / 64.f;
            insertCheckpoint = true;
        } else if (checkpoint >= 60.f / 64.f && checkpoint + 2.f * jump_quantum <= latest) {
            // The last regular air slice can still leave the feet above the
            // floor. Give the terminal interval a contact slice before its
            // short jump pulse, with room for an explicit release.
            checkpoint += jump_quantum;
            insertCheckpoint = true;
        }
        const float wanted = checkpoint + jump_quantum;
        if (wanted > latest)
            return;
        if (auto press = g_jump_press_window.reserve(cmd, wanted, latest)) {
            if (insertCheckpoint) {
                auto* step = systems::g_input.acquire_subtick_step(base);
                if (!step)
                    return;
                SetButton(step, 0);
                SetPressed(step, false);
                SetWhen(step, checkpoint);
                SetYawDelta(step, 0.f);
            }
            apply_landing_jump(base, *press);
        }
    }

} // namespace Velocity::features::movement
