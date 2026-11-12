#include "semi_rage.h"
#include "accuracy.h"
#include "trigger.h"
#include "damage.h"
#include "hitboxes.h"
#include "profiles.h"
#include "ray.h"
#include "../input_inject/input_inject.h"
#include "../bones/bones.h"
#include "../trace/trace.h"
#include "../movement/velocity_movement.h"
#include "../../interfaces/interfaces.h"
#include "../../interfaces/CUserCmd/CUserCmd.h"
#include "../../utils/cvar/cvar.h"
#include "../../utils/schema/schema.h"
#include "../../../cs2/entity/C_CSPlayerPawn/C_CSPlayerPawn.h"
#include "../../../cs2/entity/C_CSWeaponBase/C_CSWeaponBase.h"
#include "../../../cs2/entity/CCSPlayerController/CCSPlayerController.h"
#include <atomic>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <Windows.h>

namespace Legitbot {
    namespace {
        std::atomic<float> speedLimit{0.f};
        CBaseHandle scopeWeapon{};
        unsigned long long scopeRequestedAt = 0;

        struct SeedBrake {
            CBaseHandle target{}, weapon{};
            Vector_t direction{};
            float limit = 0.f;
            bool scope = false;
        } seedBrake;

        constexpr float tick = 1.f / 64.f, deg = 180.f / std::numbers::pi_v<float>;

        float DataFloat(CCSWeaponBaseVData* data, const char* field, int mode = 0) {
            const auto off = SchemaFinder::Get(hash_32_fnv1a_const(field));
            return off ? *reinterpret_cast<float*>(reinterpret_cast<std::uintptr_t>(data) + off + mode * sizeof(float))
                       : 0.f;
        }
    } // namespace

    Vector_t BrakeStep(Vector_t velocity,
                       const Vector_t& goal,
                       float wish,
                       float friction,
                       float stop,
                       float acceleration,
                       float cap) {
        velocity.z = 0.f;
        if (cap < 0.f)
            cap = wish;
        constexpr float dt = tick / 16.f;
        for (int i = 0; i < 16; i++) {
            const float speed = std::hypot(velocity.x, velocity.y);
            if (speed > 0.f)
                velocity = velocity * (std::max)(0.f, 1.f - (std::max)(stop, speed) * friction * dt / speed);
            auto difference = goal - velocity;
            const float length = difference.Length();
            if (length <= .0001f)
                continue;
            const auto direction = difference * (1.f / length);
            const float along = velocity.x * direction.x + velocity.y * direction.y;
            const float gain = (std::min)({length, acceleration * dt, (std::max)(0.f, cap - along)});
            velocity = velocity + direction * gain;
        }
        return velocity;
    }

    namespace {
        Vector_t CoastStep(
            Vector_t velocity, const Vector_t& direction, float cap, float friction, float stop, float acceleration) {
            constexpr float dt = tick / 16.f;
            velocity.z = 0.f;
            for (int i = 0; i < 16; i++) {
                const float speed = std::hypot(velocity.x, velocity.y);
                if (speed > 0.f)
                    velocity = velocity * (std::max)(0.f, 1.f - (std::max)(stop, speed) * friction * dt / speed);
                const float along = velocity.x * direction.x + velocity.y * direction.y;
                velocity = velocity + direction * (std::min)(acceleration * dt, (std::max)(0.f, cap - along));
            }
            return velocity;
        }
    } // namespace

    float PlannedSpeedLimit() {
        return speedLimit.load(std::memory_order_relaxed);
    }

    void ResetSemiRage() {
        speedLimit.store(0.f, std::memory_order_relaxed);
        scopeRequestedAt = 0;
        scopeWeapon = {};
        seedBrake = {};
    }

    void PlanSemiRage(CUserCmd* cmd,
                      C_CSPlayerPawn* local,
                      const Target& selected,
                      LegitMath::Angles angularVelocity,
                      float aimSpeed) {
        speedLimit.store(0.f, std::memory_order_relaxed);
        const auto& c = LegitProfiles::Current();
        if (!c.enabled || !c.trigger || (!c.autostop && !c.autoscope) || !selected || !local || !cmd ||
            !I::GameEntity || !I::GameEntity->Instance)
            return;
        auto* base = cmd->csgoUserCmd.pBaseCmd;
        auto* weapon = local->GetActiveWeapon();
        auto* services = local->GetWeaponServices();
        auto* target = I::GameEntity->Instance->Get<C_CSPlayerPawn>(selected.handle);
        if (!base || !base->pViewAngles || !weapon || !services || !target || target->m_iHealth() <= 0 ||
            weapon->IsNonGunWeapon() || weapon->m_iClip1() <= 0 || weapon->m_bInReload())
            return;
        auto* data = weapon->Data();
        if (!data)
            return;
        const auto handle = services->m_hActiveWeapon();
        if (handle != scopeWeapon) {
            scopeWeapon = handle;
            scopeRequestedAt = 0;
        }
        const auto live = ReadAccuracy(weapon);
        if (!live.valid)
            return;
        unsigned groups = 0;
        for (int i = 0; i < 8; i++)
            if (c.TriggerHitgroup(i))
                groups |= 1u << i;
        if (!c.seed_trigger && !(groups & (1u << selected.group)))
            return;
        Bones::GroupCapsule boxes[32]{};
        const int count = ReadHitboxes(target, selected.handle, groups, boxes, 32);
        if (!count)
            return;
        const int mode = std::clamp(weapon->m_weaponMode(), 0, 1);
        const float maximum = DataFloat(data, "CCSWeaponBaseVData->m_flMaxSpeed", mode);
        if (maximum <= 0.f)
            return;
        const float floor = maximum * .34f;
        const auto velocity = local->m_vecVelocity();
        const bool grounded = (local->m_fFlags() & FL_ONGROUND) != 0;
        const float range = data->m_flRange();
        auto eye = Bones::GetEyePos(local);
        const auto view = base->pViewAngles->angValue;
        QAngle_t recoil{};
        Velocity::ReadWeaponRecoil(local, recoil);
        // Movement planning runs before input history is finalized. Forecast from
        // the command clock; RunTrigger still validates the actual firing history.
        const int seedTick = base->nClientTick + 1;
        if (c.seed_trigger && seedTick <= 0)
            return;
        const int minimum = (std::max)(1, LegitMath::MinimumDamage(c.min_damage, target->m_iHealth()));
        const float hitchance = c.RequiredHitchance();
        const bool scoped = local->m_bIsScoped() || weapon->m_zoomLevel() > 0;
        const int type = data->m_WeaponType();
        const bool canScope =
            type == 5 || weapon->m_iItemDefinitionIndex() == 8 || weapon->m_iItemDefinitionIndex() == 39;
        const float zoomTime = DataFloat(data, "CCSWeaponBaseVData->m_flZoomTime0");
        const float friction = Cvar::Float("sv_friction", 5.2f), stop = Cvar::Float("sv_stopspeed", 80.f),
                    accel = Cvar::Float("sv_accelerate", 5.5f) * maximum;
        const float yaw = view.y / deg;
        const Vector_t forward{std::cos(yaw), std::sin(yaw), 0}, right{-std::sin(yaw), std::cos(yaw), 0};
        auto direction = forward * base->flForwardMove + right * base->flSideMove;
        const float input = direction.Length();
        if (input > .0001f)
            direction = direction * (1.f / input);
        const float horizontalSpeed = std::hypot(velocity.x, velocity.y);
        const Vector_t brakingDirection =
            !grounded && horizontalSpeed > .001f
                ? Vector_t{velocity.x / horizontalSpeed, velocity.y / horizontalSpeed, 0.f}
                : direction;
        const auto movement = reinterpret_cast<std::uintptr_t>(local->m_pMovementServices());
        const auto duckOff = SchemaFinder::Get(hash_32_fnv1a_const("CCSPlayer_MovementServices->m_flDuckAmount"));
        const float duck =
            duckOff && movement ? std::clamp(*reinterpret_cast<float*>(movement + duckOff), 0.f, 1.f) : 0.f;
        const auto walkOff = SchemaFinder::Get(hash_32_fnv1a_const("C_CSPlayerPawn->m_bIsWalking"));
        const bool walking = walkOff && *reinterpret_cast<bool*>(reinterpret_cast<std::uintptr_t>(local) + walkOff);
        const float surface = movement ? *reinterpret_cast<float*>(movement + 0x26c) : 1.f;
        const float wish = maximum * (1.f - .66f * duck);
        const float penalty = weapon->m_fAccuracyPenalty();
        const float recovery = live.recovery;
        auto* controller = I::GameEntity->Instance->Get<CCSPlayerController>(local->m_hController());
        if (!controller)
            return;
        const float weaponReadyIn =
            (std::max)(0, weapon->m_nNextPrimaryAttackTick() - static_cast<int>(controller->m_nTickBase())) * tick;
        const auto targetVelocity = target->m_vecVelocity();

        const bool canStop =
            c.autostop && (grounded || c.air_autostop) && (input > .0001f || (!grounded && horizontalSpeed > floor));
        const bool predictScoped = canScope && c.autoscope && !scoped;
        if (!c.seed_trigger || !canStop || weaponReadyIn > 0.f || seedBrake.target != selected.handle ||
            seedBrake.weapon != handle ||
            seedBrake.direction.x * brakingDirection.x + seedBrake.direction.y * brakingDirection.y < .5f)
            seedBrake = {};
        const float airAcceleration = Cvar::Float("sv_airaccelerate", 12.f) * wish * surface;
        const float airCap = (std::min)(wish, Cvar::Float("sv_air_max_wishspeed", 30.f));
        const float gravity = Cvar::Float("sv_gravity", 800.f);
        const auto origin = local->m_pGameSceneNode()->m_vecAbsOrigin();

        struct Window {
            int at = -1;
            Vector_t eye{};
            QAngle_t view{};
            bool onGround = false;
        };

        const auto accuracyAt = [&](float speed, float t, bool onGround, bool scope) {
            auto accuracy = live;
            const float ratio = std::clamp((speed / maximum - .34f) / .61f, 0.f, 1.f);
            const int futureMode = scope ? 1 : mode;
            const float moveCoefficient = DataFloat(data, "CCSWeaponBaseVData->m_flInaccuracyMove", futureMode);
            const float move = moveCoefficient * (walking ? ratio : std::pow(ratio, .25f));
            const float baseline = DataFloat(data,
                                             local->m_fFlags() & FL_DUCKING ? "CCSWeaponBaseVData->m_flInaccuracyCrouch"
                                                                            : "CCSWeaponBaseVData->m_flInaccuracyStand",
                                             futureMode);
            const float recovered = LegitMath::RecoverAccuracy(penalty, baseline, recovery, t + tick);
            accuracy.inaccuracy = (std::max)(0.f,
                                             live.inaccuracy - live.movement - penalty + move + recovered -
                                                 (!grounded && onGround ? live.air : 0.f));
            if (scope) {
                accuracy.mode = 1;
                accuracy.spread = DataFloat(data, "CCSWeaponBaseVData->m_flSpread", 1);
            }
            return accuracy;
        };
        // First predict when aim, damage, scope, weapon cooldown and trigger timers
        // can be ready. Movement accuracy must not cause braking before this window.
        const auto predictWindow = [&](float brakeSpeed,
                                       bool scope,
                                       int brakeDelay = 0,
                                       int horizon = 32,
                                       bool seedRays = true) {
            Window window;
            Vector_t simulated = velocity, offset{};
            auto angles = view;
            auto steer = angularVelocity;
            bool onGround = grounded;
            float acquisition = -1.f;
            for (int i = 0; i <= horizon; i++) {
                const float t = i * tick;
                const auto predictedEye = eye + offset;
                const auto point = selected.point + targetVelocity * t;
                const auto delta = point - predictedEye;
                const LegitMath::Angles desired{-std::atan2(delta.z, std::hypot(delta.x, delta.y)) * deg -
                                                    (c.rcs ? recoil.x * c.rcs_y * .01f : 0.f),
                                                std::atan2(delta.y, delta.x) * deg -
                                                    (c.rcs ? recoil.y * c.rcs_x * .01f : 0.f)};
                auto error = LegitMath::Delta({angles.x, angles.y}, desired);
                if (i) {
                    angles.x += c.smoothing ? LegitMath::SmoothAim(error.pitch,
                                                                   steer.pitch,
                                                                   aimSpeed * LegitMath::kAimResponseRate /
                                                                       std::clamp(c.vertical_smooth, 1.f, 30.f),
                                                                   tick)
                                            : error.pitch;
                    angles.y += c.smoothing ? LegitMath::SmoothAim(error.yaw,
                                                                   steer.yaw,
                                                                   aimSpeed * LegitMath::kAimResponseRate /
                                                                       std::clamp(c.horizontal_smooth, 1.f, 30.f),
                                                                   tick)
                                            : error.yaw;
                }
                auto fireAngles = angles;
                fireAngles.x += recoil.x;
                fireAngles.y += recoil.y;
                const float pitch = fireAngles.x / deg, yaw = fireAngles.y / deg;
                Vector_t rays[16]{{std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), -std::sin(pitch)}};
                int rayCount = 1;
                if (c.seed_trigger && seedRays) {
                    const auto accuracy = accuracyAt(std::hypot(simulated.x, simulated.y), t, onGround, scope);
                    if (!SeedDirections(fireAngles, seedTick + i, accuracy, rays))
                        break;
                    rayCount = accuracy.bullets;
                }
                float damage = 0;
                bool aligned = false;
                for (int pellet = 0; pellet < rayCount; pellet++) {
                    const auto ray = rays[pellet];
                    float nearest = range;
                    Vector_t hitPoint{};
                    int hitGroup = -1;
                    for (int j = 0; j < count; j++) {
                        const auto center = boxes[j].capsule.center + targetVelocity * t;
                        const float scale = c.TriggerPointScale(boxes[j].group);
                        if (!std::isfinite(RayCapsule(
                                predictedEye,
                                ray,
                                range,
                                center,
                                center,
                                TriggerCenterRadius(boxes[j].capsule.radius, c.TriggerMultipointEnabled(), scale))))
                            continue;
                        const auto& box = boxes[j].capsule;
                        const float distance = RayCapsule(predictedEye,
                                                          ray,
                                                          range,
                                                          box.mins + targetVelocity * t,
                                                          box.maxs + targetVelocity * t,
                                                          box.radius);
                        if (!std::isfinite(distance) || distance >= nearest)
                            continue;
                        const auto point = predictedEye + ray * (distance + .01f);
                        if (c.trigger_checks[0] && !Visible(predictedEye, point, local, target))
                            continue;
                        nearest = distance;
                        hitPoint = point;
                        hitGroup = boxes[j].group;
                    }
                    if (hitGroup >= 0) {
                        aligned = true;
                        damage += (std::max)(0.f, ShotDamage(predictedEye, hitPoint, hitGroup, local, target, weapon));
                    }
                }
                const bool scopeReady = (!scope || t >= zoomTime) && (!c.scoped_only || type != 5 || scoped || scope);
                if (aligned && scopeReady && (!c.trigger_checks[1] || onGround)) {
                    if (acquisition < 0.f)
                        acquisition = t;
                    if (damage >= minimum) {
                        if (t >= weaponReadyIn && t + 1e-5f >= TriggerReadyIn(selected.handle, acquisition)) {
                            window = {i, predictedEye, fireAngles, onGround};
                            break;
                        }
                    }
                } else
                    acquisition = -1.f;
                {
                    const float vz = simulated.z;
                    if (brakeSpeed > 0.f && i >= brakeDelay)
                        simulated = BrakeStep(simulated,
                                              (onGround ? direction : brakingDirection) * brakeSpeed,
                                              wish,
                                              onGround ? friction * surface : 0.f,
                                              stop,
                                              onGround ? accel * surface : airAcceleration,
                                              onGround ? wish : airCap);
                    else
                        simulated = CoastStep(simulated,
                                              direction,
                                              onGround ? wish : airCap,
                                              onGround ? friction * surface : 0.f,
                                              stop,
                                              onGround ? accel * surface : airAcceleration);
                    simulated.z = vz;
                }
                if (!onGround)
                    simulated.z -= gravity * tick;
                Trace::CGameTrace trace{};
                const Vector_t hullMin{-16, -16, 0}, hullMax{16, 16, 72.f - 18.f * duck};
                const auto start = origin + offset;
                if (!Trace::TraceHull(
                        start, start + simulated * tick, hullMin, hullMax, local, trace, Trace::kMaskWorld))
                    break;
                if (Trace::DidHit(trace) && trace.fraction() < .99f) {
                    if (trace.normal().z < .7f)
                        break;
                    if (!onGround && simulated.z <= 0.f) {
                        offset = trace.endpos() - origin;
                        onGround = true;
                        simulated.z = 0.f;
                        continue;
                    }
                    simulated.z = -(simulated.x * trace.normal().x + simulated.y * trace.normal().y) / trace.normal().z;
                }
                offset = offset + simulated * tick;
            }
            return window;
        };
        bool useScope = predictScoped;
        auto window = predictWindow(0.f, c.seed_trigger ? false : predictScoped);
        if (c.seed_trigger && seedBrake.limit > 0.f) {
            useScope = predictScoped && seedBrake.scope;
            window = predictWindow(seedBrake.limit, useScope);
            speedLimit.store(seedBrake.limit, std::memory_order_relaxed);
        } else if (c.seed_trigger) {
            const auto unbraked = window;
            // The opening is a movement/aim/damage constraint. Future random rays must
            // not postpone braking until after it, or start it far behind cover.
            const auto opening = predictWindow(canStop ? floor : 0.f, predictScoped, 0, 32, false);
            useScope = predictScoped;
            window = opening;
            if (unbraked.at >= 0 && unbraked.at <= 1) {
                window = unbraked;
                useScope = false;
            } else if (canStop && opening.at >= 0) {
                Vector_t braking = velocity;
                int lead = 0;
                for (; lead < 64 && std::hypot(braking.x, braking.y) > floor + .1f; lead++)
                    braking = BrakeStep(braking,
                                        brakingDirection * floor,
                                        wish,
                                        grounded ? friction * surface : 0.f,
                                        stop,
                                        grounded ? accel * surface : airAcceleration,
                                        grounded ? wish : airCap);
                if (opening.at <= lead + 1) {
                    float bestLimit = floor;
                    auto best = opening;
                    bool bestScope = predictScoped;
                    const float ceiling = (std::max)(floor, (std::min)(maximum, horizontalSpeed));
                    // Within the imminent opening, retain the fastest reachable speed whose
                    // predicted seed meets the same ray and damage conditions as the trigger.
                    for (int scope = 0; scope <= (predictScoped ? 1 : 0); scope++)
                        for (int step = 0; step <= 4; step++) {
                            const float limit = ceiling - (ceiling - floor) * step * .25f;
                            const auto candidate = predictWindow(limit, scope != 0, 0, opening.at + 1);
                            if (candidate.at >= 0 &&
                                (candidate.at < best.at || (candidate.at == best.at && limit > bestLimit))) {
                                best = candidate;
                                bestLimit = limit;
                                bestScope = scope != 0;
                            }
                        }
                    window = best;
                    useScope = bestScope;
                    speedLimit.store(bestLimit, std::memory_order_relaxed);
                    seedBrake = {selected.handle, handle, brakingDirection, bestLimit, useScope};
                }
            }
        } else if (canStop && window.at >= 0) {
            const float t = window.at * tick;
            Bones::GroupCapsule future[32];
            std::copy_n(boxes, count, future);
            for (int j = 0; j < count; j++) {
                future[j].capsule.mins = future[j].capsule.mins + targetVelocity * t;
                future[j].capsule.maxs = future[j].capsule.maxs + targetVelocity * t;
                future[j].capsule.center = future[j].capsule.center + targetVelocity * t;
            }
            const auto meetsHC = [&](float horizontalSpeed) {
                const auto accuracy = accuracyAt(horizontalSpeed, t, window.onGround, predictScoped);
                return PassesHC(window.eye,
                                window.view,
                                range,
                                accuracy,
                                future,
                                count,
                                hitchance,
                                local,
                                target,
                                minimum,
                                c.hc_budget);
            };
            if (!meetsHC(maximum)) {
                float required = floor, upper = maximum;
                if (meetsHC(floor))
                    for (int j = 0; j < 7; j++) {
                        const float middle = (required + upper) * .5f;
                        if (meetsHC(middle))
                            required = middle;
                        else
                            upper = middle;
                    }
                Vector_t braking = velocity;
                int lead = 0;
                for (; lead < 64 && std::hypot(braking.x, braking.y) > required + .1f; lead++)
                    braking = BrakeStep(braking,
                                        brakingDirection * required,
                                        wish,
                                        grounded ? friction * surface : 0.f,
                                        stop,
                                        grounded ? accel * surface : airAcceleration,
                                        grounded ? wish : airCap);
                // Braking changes when the eye clears cover. Use that trajectory, not the
                // full-speed arrival time, so accuracy is not reached far behind the edge.
                const auto brakingWindow = predictWindow(required, predictScoped);
                if (brakingWindow.at >= 0 && brakingWindow.at <= lead + 1 &&
                    (input > .0001f || horizontalSpeed > required)) {
                    speedLimit.store(required, std::memory_order_relaxed);
                    window = brakingWindow;
                }
            }
        }

        if (useScope && window.at >= 0 && window.at <= static_cast<int>(std::ceil(zoomTime / tick)) + 1 &&
            !(cmd->nButtons.nValue & IN_SECOND_ATTACK)) {
            const auto now = GetTickCount64();
            const auto wait = static_cast<unsigned long long>((std::max)(zoomTime, .1f) * 1000) + 32;
            if (!scopeRequestedAt || now - scopeRequestedAt >= wait) {
                InputInject::SetEdge(cmd, IN_SECOND_ATTACK, true);
                scopeRequestedAt = now;
            }
        }
    }
} // namespace Legitbot
