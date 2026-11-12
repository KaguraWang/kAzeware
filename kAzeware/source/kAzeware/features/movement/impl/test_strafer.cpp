// Ported from wubly/velocity @ 53ca95c120ac5ceb5e71622b5f5d0c3fff14fdcf.
#include "../velocity_sdk.h"
#include "../movement.hpp"

namespace Velocity::features::movement {

    namespace {

        constexpr auto k_max_subticks{16};
        constexpr float k_start_speed = 5.f;

        [[nodiscard]] float
        ref_ideal_angle(float speed, float dt, float wishspeed, float air_accel, float air_max_wishspeed) {
            if (speed < 1.0f) {
                return 15.0f;
            }

            air_max_wishspeed = std::min(wishspeed, air_max_wishspeed);
            const auto accel_speed = wishspeed * air_accel * dt;
            float cos_theta{};

            if (accel_speed >= air_max_wishspeed) {
                cos_theta = air_max_wishspeed / (2.0f * speed);
            } else {
                cos_theta = (air_max_wishspeed - accel_speed) / speed;
            }

            cos_theta = std::clamp(cos_theta, -1.0f, 1.0f);
            return std::fmaxf(std::acosf(cos_theta) * (180.0f / std::numbers::pi_v<float>), 1.0f);
        }

        [[nodiscard]] float ref_air_strafer(float vel_x,
                                            float vel_y,
                                            float target_yaw,
                                            float dt,
                                            bool side_switch,
                                            float wishspeed,
                                            float air_accel,
                                            float air_max_wishspeed) {
            const auto speed = std::sqrtf(vel_x * vel_x + vel_y * vel_y);
            if (speed < 15.0f) {
                return target_yaw;
            }

            const auto theta = ref_ideal_angle(speed, dt, wishspeed, air_accel, air_max_wishspeed);
            const auto vel_angle = std::atan2f(vel_y, vel_x) * (180.0f / std::numbers::pi_v<float>);
            auto vel_delta = target_yaw - vel_angle;
            math::helpers::normalize_angle(vel_delta);

            if (std::fabsf(vel_delta) > 2.0f) {
                if (vel_delta > 0.0f) {
                    auto yaw = vel_angle + theta;
                    math::helpers::normalize_angle(yaw);
                    return yaw;
                }

                auto yaw = vel_angle - theta;
                math::helpers::normalize_angle(yaw);
                return yaw;
            }

            if (side_switch) {
                auto yaw = vel_angle + theta;
                math::helpers::normalize_angle(yaw);
                return yaw;
            }

            auto yaw = vel_angle - theta;
            math::helpers::normalize_angle(yaw);
            return yaw;
        }

        void ref_air_accel_sim(float& vel_x,
                               float& vel_y,
                               float wishdir_yaw,
                               float frame_time,
                               float friction,
                               float wishspeed,
                               float air_accel,
                               float air_max_wishspeed) {
            const auto yaw_rad = wishdir_yaw * (std::numbers::pi_v<float> / 180.0f);
            const auto wish_dir_x = std::cosf(yaw_rad);
            const auto wish_dir_y = std::sinf(yaw_rad);

            const auto capped = std::fminf(wishspeed, air_max_wishspeed);
            const auto dot = vel_x * wish_dir_x + vel_y * wish_dir_y;
            const auto add_speed = capped - dot;

            if (add_speed <= 0.0f) {
                return;
            }

            const auto accel_speed = wishspeed * air_accel * friction * frame_time;
            const auto step = std::fminf(accel_speed, add_speed);

            vel_x += wish_dir_x * step;
            vel_y += wish_dir_y * step;
        }

    } // namespace

    [[nodiscard]] bool test_strafer::is_active() const {
        if (!keybind.isActive(Config::movement.airstrafe)) {
            return false;
        }

        return true;
    }

    math::vector2 test_strafer::movement_from_buttons(std::uintptr_t pressed) {
        auto forward_move{0.0f};
        auto left_move{0.0f};

        if (pressed & cstypes::command_buttons::in_forward) {
            forward_move = 1.0f;
        } else if (pressed & cstypes::command_buttons::in_back) {
            forward_move = -1.0f;
        }

        if (pressed & cstypes::command_buttons::in_moveleft) {
            left_move = -1.0f;
        } else if (pressed & cstypes::command_buttons::in_moveright) {
            left_move = 1.0f;
        }

        return {forward_move, left_move};
    }

    void test_strafer::on_create_move(systems::input::usercmd* cmd) {
        this->m_handled_this_tick = false;

        if (!this->is_active()) {
            return;
        }

        if (features::movement::g_jumpbug.active_this_tick()) {
            return;
        }

        const auto base = cmd->csgoUserCmd.pBaseCmd;
        if (!base) {
            return;
        }

        const auto local = systems::g_local.get();
        if (!local.pawn) {
            return;
        }

        const auto move_type = memory::read<std::uint8_t>(local.pawn + Offsets::CBaseEntity_m_nActualMoveType);
        if (move_type == cstypes::move_type::ladder || move_type == cstypes::move_type::noclip) {
            return;
        }

        const auto& prestate = systems::g_prediction.pre();
        if (prestate.flags & cstypes::entity_flags::on_ground) {
            return;
        }

        this->quantized_path(cmd);
    }

    CSubtickMoveStep* test_strafer::apply_yaw_subtick(proto::base_usercmd_pb* base, float when, float yaw_delta) const {
        math::helpers::normalize_angle(yaw_delta);

        const auto step = systems::g_input.acquire_subtick_step(base);
        if (!step) {
            return nullptr;
        }

        SetWhen(step, when);
        SetButton(step, 0);
        SetPressed(step, false);
        SetForwardDelta(step, 0.0f);
        SetLeftDelta(step, 0.0f);
        SetYawDelta(step, yaw_delta);
        SetPitchDelta(step, 0.0f);
        return step;
    }

    void test_strafer::quantized_path(systems::input::usercmd* cmd) {
        const auto current_buttons = cmd->nButtons.nValue;
        if (current_buttons & static_cast<std::uintptr_t>(cstypes::command_buttons::in_sprint)) {
            return;
        }

        const auto base = cmd->csgoUserCmd.pBaseCmd;
        if (!base) {
            return;
        }

        const auto previous_pressed = this->m_last_pressed;
        this->check_button(current_buttons, cstypes::command_buttons::in_moveleft);
        this->check_button(current_buttons, cstypes::command_buttons::in_moveright);
        this->check_button(current_buttons, cstypes::command_buttons::in_forward);
        this->check_button(current_buttons, cstypes::command_buttons::in_back);
        this->m_last_buttons = current_buttons;

        const auto& prestate = systems::g_prediction.pre();
        const auto velocity = prestate.networked_velocity;
        const auto command_yaw = base->pViewAngles->angValue.y;

        const auto player_move = movement_from_buttons(this->m_last_pressed);
        if (player_move.x == 0.0f && player_move.y == 0.0f) {
            return;
        }

        if (!Convar<bool>("sv_quantize_movement_input"))
            return;

        // Jump/button events share this list but do not postpone air movement.
        // Both timelines are merged by the final chronological subtick sort.
        constexpr auto start_when = 0.f;

        const auto sv_airaccelerate = Convar<float>("sv_airaccelerate");
        const auto services =
            memory::read<std::uintptr_t>(systems::g_local.get().pawn + Offsets::C_BasePlayerPawn_m_pMovementServices);
        const auto sv_maxspeed = AirWishSpeed(services);
        const auto sv_air_max_wishspeed = Convar<float>("sv_air_max_wishspeed");
        const auto surface_friction = prestate.surface_friction;

        const auto base_yaw_offset = std::atan2f(-player_move.y, player_move.x) * (180.0f / std::numbers::pi_v<float>);
        auto target_yaw = command_yaw + base_yaw_offset;
        math::helpers::normalize_angle(target_yaw);
        const auto wall_normal = AirWallNormal(systems::g_local.get().pawn, services, prestate, target_yaw);
        auto strafe_velocity = velocity - wall_normal * velocity.dot(wall_normal);
        const float wall_heading =
            std::atan2(strafe_velocity.y, strafe_velocity.x) * (180.f / std::numbers::pi_v<float>);
        target_yaw = AirWallYaw(target_yaw, wall_normal, wall_heading);
        const float initial_yaw = velocity.length_2d() >= k_start_speed
                                      ? std::atan2(velocity.y, velocity.x) * (180.f / std::numbers::pi_v<float>)
                                      : target_yaw;
        float initial_delta = target_yaw - initial_yaw;
        math::helpers::normalize_angle(initial_delta);
        if (previous_pressed != this->m_last_pressed || (!this->m_reversing && std::abs(initial_delta) > 2.f)) {
            this->m_reversing = std::abs(initial_delta) > 2.f;
            this->m_turn_direction = initial_delta >= 0.f ? 1 : -1;
            // Exact opposites have two equal arcs. Keep the selected side stable
            // instead of letting tiny velocity rounding reverse it each sample.
            if (std::abs(initial_delta) > 178.f) {
                if (this->m_last_pressed & cstypes::command_buttons::in_moveright)
                    this->m_turn_direction = -1;
                else if (this->m_last_pressed & cstypes::command_buttons::in_moveleft)
                    this->m_turn_direction = 1;
            }
        }

        // The endpoint controls the last physical interval. Keep that interval
        // in movement space; the native terminal sample restores the base view.
        auto* final_step = systems::g_input.acquire_subtick_step(base);
        if (!final_step)
            return;
        const float final_when = std::nextafter(1.f, 0.f);
        SetButton(final_step, 0);
        SetPressed(final_step, false);
        SetWhen(final_step, final_when);
        SetForwardDelta(final_step, 0.f);
        SetLeftDelta(final_step, 0.f);
        SetPitchDelta(final_step, 0.f);
        SetYawDelta(final_step, 0.f);
        const auto when_step = (final_when - start_when) / static_cast<float>(k_max_subticks);
        const auto sub_frame = cstypes::tick_interval * when_step;

        auto acc_yaw = command_yaw;
        auto sim_vx = strafe_velocity.x;
        auto sim_vy = strafe_velocity.y;
        auto injected = 0;

        for (auto i = 0; i < k_max_subticks; ++i) {
            const auto entry_side = ((this->m_substep_counter + i) % 2) == 0;
            auto wishdir_yaw = ref_air_strafer(sim_vx,
                                               sim_vy,
                                               target_yaw,
                                               sub_frame,
                                               entry_side,
                                               sv_maxspeed,
                                               sv_airaccelerate * surface_friction,
                                               sv_air_max_wishspeed);
            const float speed = std::hypot(sim_vx, sim_vy);
            if (this->m_reversing) {
                const float velocity_yaw = std::atan2(sim_vy, sim_vx) * (180.f / std::numbers::pi_v<float>);
                float delta = target_yaw - velocity_yaw;
                math::helpers::normalize_angle(delta);
                if (std::abs(delta) <= 2.f || delta * this->m_turn_direction < 0.f)
                    this->m_reversing = false;
                else if (speed >= k_start_speed) {
                    const float speed_angle = ref_ideal_angle(
                        speed, sub_frame, sv_maxspeed, sv_airaccelerate * surface_friction, sv_air_max_wishspeed);
                    wishdir_yaw = velocity_yaw + this->m_turn_direction * AirTurnAngle(speed_angle, delta);
                }
            }

            if (Config::movement.turn_radius > 0.f && speed >= k_start_speed &&
                (this->m_reversing || std::abs(initial_delta) > 2.f)) {
                const float heading = std::atan2(sim_vy, sim_vx) * (180.f / std::numbers::pi_v<float>);
                float delta = target_yaw - heading;
                math::helpers::normalize_angle(delta);
                const int side = this->m_reversing ? this->m_turn_direction : (delta >= 0.f ? 1 : -1);
                const float ideal = ref_ideal_angle(
                    speed, sub_frame, sv_maxspeed, sv_airaccelerate * surface_friction, sv_air_max_wishspeed);
                const float turn = AirRadiusAngle(speed,
                                                  delta,
                                                  AirTurnAngle(ideal, delta),
                                                  sub_frame,
                                                  sv_maxspeed,
                                                  sv_airaccelerate,
                                                  sv_air_max_wishspeed,
                                                  surface_friction);
                wishdir_yaw = heading + side * turn;
            }
            wishdir_yaw = AirWallYaw(wishdir_yaw, wall_normal, target_yaw);
            auto target_view_yaw = wishdir_yaw - base_yaw_offset;
            math::helpers::normalize_angle(target_view_yaw);

            auto yaw_delta = target_view_yaw - acc_yaw;
            math::helpers::normalize_angle(yaw_delta);

            const auto when_frac = start_when + static_cast<float>(i) * when_step;

            if (i == 0 || yaw_delta != 0.f) {
                auto* step = this->apply_yaw_subtick(base, when_frac, yaw_delta);
                if (!step)
                    break;
                if (i == 0) {
                    // A yaw stream starts from the last simulated axes. Establish
                    // the newly held digital direction even when the first yaw is zero.
                    SetForwardDelta(step, player_move.x - prestate.last_movement_impulses.x);
                    SetLeftDelta(step, -player_move.y - prestate.last_movement_impulses.y);
                    SetForward(base, player_move.x);
                    SetLeft(base, -player_move.y);
                }
            }

            acc_yaw = target_view_yaw;
            ref_air_accel_sim(sim_vx,
                              sim_vy,
                              wishdir_yaw,
                              sub_frame,
                              surface_friction,
                              sv_maxspeed,
                              sv_airaccelerate,
                              sv_air_max_wishspeed);
            ++injected;
        }

        if (injected > 0) {
            this->m_handled_this_tick = true;
            ++this->m_substep_counter;
        } else
            --base->subtickMovesField.nCurrentSize;
    }

    void test_strafer::check_button(std::uintptr_t current_buttons, std::uintptr_t button) {
        constexpr auto moveleft = static_cast<std::uintptr_t>(cstypes::command_buttons::in_moveleft);
        constexpr auto moveright = static_cast<std::uintptr_t>(cstypes::command_buttons::in_moveright);
        constexpr auto forward = static_cast<std::uintptr_t>(cstypes::command_buttons::in_forward);
        constexpr auto back = static_cast<std::uintptr_t>(cstypes::command_buttons::in_back);

        if (current_buttons & button &&
            (!(this->m_last_buttons & button) || (button & moveleft && !(this->m_last_pressed & moveright)) ||
             (button & moveright && !(this->m_last_pressed & moveleft)) ||
             (button & forward && !(this->m_last_pressed & back)) ||
             (button & back && !(this->m_last_pressed & forward)))) {
            if (button & moveleft) {
                this->m_last_pressed &= ~moveright;
            } else if (button & moveright) {
                this->m_last_pressed &= ~moveleft;
            } else if (button & forward) {
                this->m_last_pressed &= ~back;
            } else if (button & back) {
                this->m_last_pressed &= ~forward;
            }

            this->m_last_pressed |= button;
        } else if (!(current_buttons & button)) {
            this->m_last_pressed &= ~button;
        }
    }

} // namespace Velocity::features::movement
