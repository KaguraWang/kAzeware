#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <numbers>
#include <limits>
#include <optional>
#include <type_traits>
#include "../../interfaces/CUserCmd/CUserCmd.h"
#include "../../config/config.h"
#include "../../keybinds/keybinds.h"
#include "../../utils/cvar/cvar.h"
#include "../../utils/schema/schema.h"
#include "../../utils/console/console.h"
#include "../input_inject/input_inject.h"

// Concrete SDK bindings for the imported Velocity movement implementations.
namespace Velocity {
    namespace math {
        struct vector2 {
            float x = 0, y = 0;

            float length() const {
                return std::sqrt(x * x + y * y);
            }
        };

        struct vector3 : Vector_t {
            using Vector_t::Vector_t;
            using Vector_t::operator=;
            vector3() = default;

            vector3(const Vector_t& v) : Vector_t(v.x, v.y, v.z) {}

            float length_2d() const {
                return std::sqrt(x * x + y * y);
            }

            float dot(const Vector_t& v) const {
                return x * v.x + y * v.y + z * v.z;
            }
        };

        static_assert(sizeof(vector3) == 12);

        namespace helpers {
            inline void normalize_angle(float& a) {
                while (a > 180.f)
                    a -= 360.f;
                while (a < -180.f)
                    a += 360.f;
            }

            inline void normalize_angles(vector3& a) {
                normalize_angle(a.y);
                while (a.x > 89.f)
                    a.x -= 180.f;
                while (a.x < -89.f)
                    a.x += 180.f;
                a.z = 0.f;
            }

            inline void angle_vectors_2d(float yaw, vector3& f, vector3& r) {
                float t = yaw * std::numbers::pi_v<float> / 180.f;
                f = {std::cos(t), std::sin(t), 0.f};
                r = {-std::sin(t), std::cos(t), 0.f};
            }
        } // namespace helpers
    } // namespace math

    namespace memory {
        template <class T> T read(std::uintptr_t a) {
            return *reinterpret_cast<const T*>(a);
        }
    } // namespace memory

    namespace cstypes {
        constexpr float tick_interval = 1.f / 64.f;

        namespace entity_flags {
            constexpr std::uint32_t on_ground = FL_ONGROUND;
        }

        namespace move_type {
            constexpr auto ladder = MOVETYPE_LADDER, noclip = MOVETYPE_NOCLIP;
        }

        namespace command_buttons {
            constexpr std::uint64_t in_jump = IN_JUMP, in_duck = IN_DUCK, in_forward = IN_FORWARD, in_back = IN_BACK;
            constexpr std::uint64_t in_moveleft = IN_MOVELEFT, in_moveright = IN_MOVERIGHT, in_sprint = IN_SPRINT;
        } // namespace command_buttons
    } // namespace cstypes

    namespace proto {
        using base_usercmd_pb = CBaseUserCmdPB;
    }

    namespace Offsets {
        inline std::uint32_t CBaseEntity_m_nActualMoveType = 0;
        inline std::uint32_t CCSPlayerModernJump_m_flLastLandedVelocityZ = 0;
        inline std::uint32_t CCSPlayerModernJump_m_nLastLandedTick = 0;
        inline std::uint32_t CCSPlayer_MovementServices_m_ModernJump = 0;
        inline std::uint32_t CCSPlayer_MovementServices_m_flDuckAmount = 0;
        inline std::uint32_t CCSPlayer_MovementServices_m_bDucked = 0;
        inline std::uint32_t CCSPlayer_MovementServices_m_flDuckSpeed = 0;
        inline std::uint32_t CCSPlayer_MovementServices_m_flStamina = 0;
        inline std::uint32_t CCSWeaponBaseVData_m_flMaxSpeed = 0;
        inline std::uint32_t CCollisionProperty_m_vecMaxs = 0;
        inline std::uint32_t CCollisionProperty_m_vecMins = 0;
        inline std::uint32_t CGameSceneNode_m_vecAbsOrigin = 0;
        inline std::uint32_t CPlayer_MovementServices_m_flMaxspeed = 0;
        inline std::uint32_t C_BaseEntity_m_flGravityScale = 0;
        inline std::uint32_t C_BaseEntity_m_nActualMoveType = 0;
        inline std::uint32_t C_BaseEntity_m_pGameSceneNode = 0;
        inline std::uint32_t C_BaseModelEntity_m_Collision = 0;
        inline std::uint32_t C_BasePlayerPawn_m_pMovementServices = 0;
    } // namespace Offsets

    struct ConvarDefault {
        const char* name;
        float value;
    };

    inline constexpr ConvarDefault kConvars[] = {{"sv_accelerate", 5.5f},
                                                 {"sv_air_max_wishspeed", 30.f},
                                                 {"sv_airaccelerate", 12.f},
                                                 {"sv_autobunnyhopping", 0.f},
                                                 {"sv_friction", 5.2f},
                                                 {"sv_gravity", 800.f},
                                                 {"sv_legacy_jump", 0.f},
                                                 {"sv_maxspeed", 320.f},
                                                 {"sv_quantize_movement_input", 1.f},
                                                 {"sv_staminarecoveryrate", 60.f},
                                                 {"sv_standable_normal", .7f},
                                                 {"sv_stopspeed", 80.f},
                                                 {"sv_jump_spam_penalty_time", 1.f / 64.f},
                                                 {"sv_bhop_time_window", .5f / 64.f},
                                                 {"sv_enablebunnyhopping", 0.f}};

    template <class T> T Convar(const char* name) {
        for (const auto& c : kConvars)
            if (!std::strcmp(c.name, name)) {
                if constexpr (std::is_same_v<T, bool>)
                    return Cvar::Bool(c.name, c.value != 0.f);
                else
                    return static_cast<T>(Cvar::Float(c.name, c.value));
            }
        return T{};
    }

    // Native CheckParameters crops both wish movement and its maximum speed while
    // ducking (build 14192, client+8CA655). m_flMaxspeed itself remains unchanged.
    inline float AirWishSpeed(std::uintptr_t services) {
        const float speed = (std::min)(Convar<float>("sv_maxspeed"),
                                       memory::read<float>(services + Offsets::CPlayer_MovementServices_m_flMaxspeed));
        const float duck =
            std::clamp(memory::read<float>(services + Offsets::CCSPlayer_MovementServices_m_flDuckAmount), 0.f, 1.f);
        return speed * (1.f - 0.66f * duck);
    }

    inline float AirTurnAngle(float speed_angle, float yaw_delta) {
        const float balance = Config::movement.turn_balance * 0.01f;
        const float response_angle = 90.f + (std::min)(45.f, std::abs(yaw_delta) * 0.5f);
        return balance <= 0.5f ? std::lerp(speed_angle, 90.f, balance * 2.f)
                               : std::lerp(90.f, response_angle, (balance - 0.5f) * 2.f);
    }

    // Choose a wish direction whose simulated heading change follows ds / radius.
    // Search the increasing part of the native air-acceleration curve; a requested
    // radius tighter than the server can produce uses the available turn instead.
    inline float AirRadiusAngle(float speed,
                                float yaw_delta,
                                float wish_angle,
                                float dt,
                                float wishspeed,
                                float air_accel,
                                float cap,
                                float friction) {
        const float radius = Config::movement.turn_radius;
        if (radius <= 0.f || speed <= 0.f)
            return wish_angle;
        constexpr float radians = std::numbers::pi_v<float> / 180.f;
        const float accel = (std::max)(0.f, wishspeed * air_accel * friction * dt);
        const float target = (std::min)(std::abs(yaw_delta) * radians, speed * dt / radius);
        float low = 0.f;
        float high = (std::min)(wish_angle * radians, std::acos(-std::clamp(accel / speed, 0.f, 1.f)));
        cap = (std::min)(wishspeed, cap);
        for (int i = 0; i < 18; ++i) {
            const float angle = (low + high) * 0.5f;
            const float cosine = std::cos(angle), sine = std::sin(angle);
            const float gain = std::clamp(cap - speed * cosine, 0.f, accel);
            const float turn = std::atan2(gain * sine, speed + gain * cosine);
            if (turn < target)
                low = angle;
            else
                high = angle;
        }
        return (low + high) * 0.5f / radians;
    }

    inline CSubtickMoveStep* StepAt(CBaseUserCmdPB* b, int i) {
        return b->subtickMovesField.pRep->tElements[i];
    }

    inline void SetForward(CBaseUserCmdPB* b, float v) {
        b->flForwardMove = v;
        b->SetBits(BASE_BITS_FORWARDMOVE);
    }

    inline void SetLeft(CBaseUserCmdPB* b, float v) {
        b->flSideMove = v;
        b->SetBits(BASE_BITS_LEFTMOVE);
    }

    inline void SetButton(CSubtickMoveStep* s, std::uint64_t v) {
        s->nButton = v;
        s->SetBits(MOVESTEP_BITS_BUTTON);
    }

    inline void SetPressed(CSubtickMoveStep* s, bool v) {
        s->bPressed = v;
        s->SetBits(MOVESTEP_BITS_PRESSED);
    }

    inline void SetWhen(CSubtickMoveStep* s, float v) {
        s->flWhen = v;
        s->SetBits(MOVESTEP_BITS_WHEN);
    }

    inline void SetForwardDelta(CSubtickMoveStep* s, float v) {
        s->flAnalogForwardDelta = v;
        s->SetBits(MOVESTEP_BITS_ANALOG_FORWARD_DELTA);
    }

    inline void SetLeftDelta(CSubtickMoveStep* s, float v) {
        s->flAnalogLeftDelta = v;
        s->SetBits(MOVESTEP_BITS_ANALOG_LEFT_DELTA);
    }

    inline void SetPitchDelta(CSubtickMoveStep* s, float v) {
        s->flPitchDelta = v;
        s->SetBits(MOVESTEP_BITS_PITCH_DELTA);
    }

    inline void SetYawDelta(CSubtickMoveStep* s, float v) {
        s->flYawDelta = v;
        s->SetBits(MOVESTEP_BITS_YAW_DELTA);
    }

    namespace systems {
        struct local {
            struct snapshot {
                std::uintptr_t pawn = 0;
            };

            snapshot get() const;
        };

        struct input {
            using usercmd = CUserCmd;

            CSubtickMoveStep* acquire_subtick_step(CBaseUserCmdPB* b) const {
                return InputInject::AcquireStep(b);
            }

            math::vector3 get_view_angles() const;
        };

        struct prediction {
            struct state {
                std::uint64_t buttons = 0;
                std::uint32_t flags = 0;
                math::vector3 networked_velocity{}, velocity{}, networked_origin{}, origin{}, last_movement_impulses{};
                float stamina = 0, surface_friction = 1;
            };

            state current{};

            const state& pre() const {
                return current;
            }
        };

        struct tracing {
            struct bbox_collision {
                math::vector3 mins, maxs;
            };

            struct alignas(16) player_movement_filter {
                std::byte data[0x80]{};
            };

            struct result {
                float fraction = 1.f;
                math::vector3 normal{}, position{};
            };

            player_movement_filter
            make_player_movement_filter(std::uintptr_t pawn, std::uintptr_t mask, int group) const;
            result trace_player_bbox(const math::vector3& start,
                                     const math::vector3& end,
                                     const bbox_collision& box,
                                     const player_movement_filter& filter,
                                     std::uintptr_t services) const;
        };

        inline local g_local{};
        inline input g_input{};
        inline prediction g_prediction{};
        inline tracing g_tracing{};
    } // namespace systems

    inline math::vector3 AirWallNormal(std::uintptr_t pawn,
                                       std::uintptr_t services,
                                       const systems::prediction::state& pre,
                                       float target_yaw) {
        const float speed = pre.networked_velocity.length_2d();
        if (speed <= 5.f)
            return {};
        const float angle = target_yaw * (std::numbers::pi_v<float> / 180.f);
        const math::vector3 direction{std::cos(angle), std::sin(angle), 0.f};
        const auto mins = memory::read<math::vector3>(pawn + Offsets::C_BaseModelEntity_m_Collision +
                                                      Offsets::CCollisionProperty_m_vecMins);
        const auto maxs = memory::read<math::vector3>(pawn + Offsets::C_BaseModelEntity_m_Collision +
                                                      Offsets::CCollisionProperty_m_vecMaxs);
        const auto filter =
            systems::g_tracing.make_player_movement_filter(pawn, memory::read<std::uintptr_t>(pawn + 0xd50), 11);
        auto hit = systems::g_tracing.trace_player_bbox(
            pre.origin, pre.origin + pre.networked_velocity * cstypes::tick_interval, {mins, maxs}, filter, services);
        // The next movement sweep finds incoming impacts. A short contact query
        // also covers a wall that has already clipped the inward velocity to zero.
        if (hit.fraction >= 1.f || std::abs(hit.normal.z) >= 0.7f) {
            const float cross = pre.networked_velocity.x * direction.y - pre.networked_velocity.y * direction.x;
            if (cross == 0.f)
                return {};
            const float side = cross > 0.f ? 1.f : -1.f;
            const math::vector3 contact{
                -pre.networked_velocity.y / speed * side, pre.networked_velocity.x / speed * side, 0.f};
            hit =
                systems::g_tracing.trace_player_bbox(pre.origin, pre.origin + contact, {mins, maxs}, filter, services);
        }
        if (hit.fraction >= 1.f || std::abs(hit.normal.z) >= 0.7f)
            return {};
        const float length = std::hypot(hit.normal.x, hit.normal.y);
        if (length == 0.f)
            return {};
        const math::vector3 normal{hit.normal.x / length, hit.normal.y / length, 0.f};
        if (direction.dot(normal) >= 0.f)
            return {};
        const math::vector3 tangent = pre.networked_velocity - normal * pre.networked_velocity.dot(normal);
        // Head-on impacts remain governed by native collision physics.
        return tangent.length_2d() > 5.f ? normal : math::vector3{};
    }

    inline float AirWallYaw(float yaw, const math::vector3& normal, float fallback_yaw) {
        if (normal.x == 0.f && normal.y == 0.f)
            return yaw;
        const float angle = yaw * (std::numbers::pi_v<float> / 180.f);
        const math::vector3 direction{std::cos(angle), std::sin(angle), 0.f};
        const math::vector3 tangent = direction - normal * direction.dot(normal);
        return tangent.length_2d() > 0.001f ? std::atan2(tangent.y, tangent.x) * (180.f / std::numbers::pi_v<float>)
                                            : fallback_yaw;
    }

    namespace xdraw {
        class draw_list;
    }

    struct WeaponContext {
        bool valid = false;
        std::uintptr_t weapon_vdata = 0;
        float weapon_max_speed = 250.f;
    };

    const WeaponContext& CombatContext();
    std::uintptr_t GlobalVarsAddress();
    bool Init();
    bool Run(CUserCmd* cmd, void* pawn);
    void Reset();
    void RestoreViewAngles(const QAngle_t& angles);
} // namespace Velocity
