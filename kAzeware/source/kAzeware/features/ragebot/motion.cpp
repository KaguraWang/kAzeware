#include "motion.h"
#include "rage.h"
#include "../legitbot/damage.h"
#include "../legitbot/hitboxes.h"
#include "../legitbot/ray.h"
#include "../movement/brake_math.h"
#include "../movement/velocity_movement.h"
#include "history.h"
#include "shots.h"
#include "shot_plan.h"
#include "../legitbot/accuracy.h"
#include "../legitbot/legit_math.h"
#include "../input_inject/input_inject.h"
#include "../trace/trace.h"
#include "../sdk_prio_a/sdk_prio_a.h"
#include "../../interfaces/CUserCmd/CUserCmd.h"
#include "../../interfaces/interfaces.h"
#include "../../utils/cvar/cvar.h"
#include "../../utils/schema/schema.h"
#include "../../config/config.h"
#include "../../../cs2/entity/C_CSPlayerPawn/C_CSPlayerPawn.h"
#include "../../../cs2/entity/C_CSWeaponBase/C_CSWeaponBase.h"
#include "../../../cs2/entity/CCSPlayerController/CCSPlayerController.h"
#include <atomic>
#include <numbers>
#include <bit>
#include <cstring>
#include <optional>

namespace Rage {
    namespace {
        constexpr float tick = 1.f / 64.f, rad = std::numbers::pi_v<float> / 180.f;
        std::atomic<float> limit{-1.f};
        std::atomic<float> brakePhase{0.f};
        CBaseHandle brakingTarget{};
        float initialSpeed = 0.f;
        bool braking = false;

        struct PreparationCursor {
            ScanCursor centers{}, edges{};
        };

        std::array<PreparationCursor, 65> preparationDamageCursors{}, preparationReadyCursors{};

        float Data(CCSWeaponBaseVData* data, const char* name, int mode = 0) {
            const auto offset = SchemaFinder::Get(hash_32_fnv1a_const(name));
            return offset ? *reinterpret_cast<float*>(reinterpret_cast<std::uintptr_t>(data) + offset + 4 * mode) : 0.f;
        }

        float Duck(C_CSPlayerPawn* local) {
            auto* service = local->m_pMovementServices();
            const auto offset = SchemaFinder::Get(hash_32_fnv1a_const("CCSPlayer_MovementServices->m_flDuckAmount"));
            return service && offset
                       ? std::clamp(
                             *reinterpret_cast<float*>(reinterpret_cast<std::uintptr_t>(service) + offset), 0.f, 1.f)
                       : 0.f;
        }

        Vector_t Direction(CBaseUserCmdPB* base) {
            const float yaw = base->pViewAngles->angValue.y * rad;
            Vector_t result{std::cos(yaw) * base->flForwardMove - std::sin(yaw) * base->flSideMove,
                            std::sin(yaw) * base->flForwardMove + std::cos(yaw) * base->flSideMove,
                            0};
            float n = result.Length();
            return n > .001f ? result * (1.f / n) : Vector_t{};
        }

        struct MotionState {
            Vector_t origin{}, velocity{};
            bool grounded = false;
            Vector_t groundNormal{0, 0, 1};
        };

        struct Physics {
            float wish = 0, groundAcceleration = 0, airAcceleration = 0, friction = 0, stop = 0, airCap = 0,
                  gravity = 0, height = 72;
            std::array<float, 3> groundNormal{0, 0, 1};
            bool operator==(const Physics&) const = default;
        };

        struct HullKey {
            std::uintptr_t pawn = 0;
            std::array<float, 7> values{};
            bool operator==(const HullKey&) const = default;
        };

        struct HullResult {
            HullKey key{};
            Trace::CGameTrace trace{};
            std::uint64_t epoch = 0;
            bool success = false;
        };

        std::array<HullResult, 512> hullCache{};
        std::uint64_t hullEpoch = 1;

        bool MovementTrace(
            const Vector_t& start, const Vector_t& end, C_CSPlayerPawn* pawn, float height, Trace::CGameTrace& trace) {
            const HullKey key{reinterpret_cast<std::uintptr_t>(pawn),
                              {start.x, start.y, start.z, end.x, end.y, end.z, height}};
            std::uint32_t hash = static_cast<std::uint32_t>(key.pawn >> 4);
            for (float value : key.values)
                hash = (hash ^ std::bit_cast<std::uint32_t>(value)) * 16777619u;
            hash ^= hash >> 16;
            hash *= 0x7feb352du;
            hash ^= hash >> 15;
            hash *= 0x846ca68bu;
            hash ^= hash >> 16;
            auto& cached = hullCache[hash & (hullCache.size() - 1)];
            if (cached.epoch == hullEpoch && cached.key == key) {
                trace = cached.trace;
                return cached.success;
            }
            const bool success = Velocity::TracePlayerHull(pawn, start, end, height, trace);
            cached = {key, trace, hullEpoch, success};
            return success;
        }

        Physics Parameters(C_CSPlayerPawn* pawn, std::optional<float> maximum = {}) {
            auto* service = pawn->m_pMovementServices();
            const float surface =
                service ? *reinterpret_cast<float*>(reinterpret_cast<std::uintptr_t>(service) + 0x26c) : 1.f;
            const auto maxOffset = SchemaFinder::Get(hash_32_fnv1a_const("CPlayer_MovementServices->m_flMaxspeed"));
            const float nativeMax =
                service && maxOffset ? *reinterpret_cast<float*>(reinterpret_cast<std::uintptr_t>(service) + maxOffset)
                                     : maximum.value_or(250.f);
            // Movement services retain the player's general maximum (260 for SSG08).
            // Native movement also crops local input to the held weapon's maximum.
            Physics p;
            p.wish = (std::min)(nativeMax, Cvar::Float("sv_maxspeed", 320.f));
            if (maximum)
                p.wish = (std::min)(p.wish, *maximum);
            p.wish *= 1.f - .66f * Duck(pawn);
            const float groundSurface = (pawn->m_fFlags() & FL_ONGROUND) ? surface : 1.f;
            p.groundAcceleration = Cvar::Float("sv_accelerate", 5.5f) * p.wish * groundSurface;
            p.airAcceleration = Cvar::Float("sv_airaccelerate", 12.f) * p.wish;
            p.friction = Cvar::Float("sv_friction", 5.2f) * groundSurface;
            p.stop = Cvar::Float("sv_stopspeed", 80.f);
            p.airCap = (std::min)(p.wish, Cvar::Float("sv_air_max_wishspeed", 30.f));
            p.height = 72.f - 18.f * Duck(pawn);
            const auto gravityOffset = SchemaFinder::Get(hash_32_fnv1a_const("C_BaseEntity->m_flGravityScale"));
            const float scale =
                gravityOffset ? *reinterpret_cast<float*>(reinterpret_cast<std::uintptr_t>(pawn) + gravityOffset) : 1.f;
            const auto normalOffset =
                SchemaFinder::Get(hash_32_fnv1a_const("CPlayer_MovementServices_Humanoid->m_groundNormal"));
            Vector_t normal{};
            if (service && normalOffset && Mem::ReadField(service, normalOffset, normal) && std::isfinite(normal.x) &&
                std::isfinite(normal.y) && std::isfinite(normal.z) &&
                normal.z >= Cvar::Float("sv_standable_normal", .7f))
                p.groundNormal = {normal.x, normal.y, normal.z};
            p.gravity = Cvar::Float("sv_gravity", 800.f) * scale;
            return p;
        }

        bool MoveHull(MotionState& state, const Vector_t& delta, C_CSPlayerPawn* pawn, const Physics& p) {
            const auto start = state;
            const bool supported = start.grounded;
            const float standable = Cvar::Float("sv_standable_normal", .7f),
                        step = (std::max)(0.f, Cvar::Float("sv_stepsize", 18.f));
            const auto trace = [&](const Vector_t& from, const Vector_t& to, Trace::CGameTrace& hit) {
                return MovementTrace(from, to, pawn, p.height, hit) && !hit.startsolid();
            };
            const auto slide = [&](MotionState& value, Vector_t remaining, bool& wall) {
                for (int bump = 0; bump < 4 && remaining.Length() > .0001f; ++bump) {
                    Trace::CGameTrace hit{};
                    if (!trace(value.origin, value.origin + remaining, hit))
                        return false;
                    value.origin = hit.endpos();
                    if (!Trace::DidHit(hit))
                        break;
                    const auto normal = hit.normal();
                    wall |= normal.z < standable;
                    remaining = remaining * (1.f - hit.fraction());
                    const float into = Legitbot::Dot(remaining, normal);
                    if (into < 0.f)
                        remaining = remaining - normal * into;
                    const float velocityInto = Legitbot::Dot(value.velocity, normal);
                    if (velocityInto < 0.f)
                        value.velocity = value.velocity - normal * velocityInto;
                }
                return true;
            };
            bool wall = false;
            if (!slide(state, delta, wall))
                return false;
            if (supported && wall && step > 0.f) {
                Trace::CGameTrace up{};
                if (trace(start.origin, start.origin + Vector_t{0, 0, step}, up) &&
                    up.endpos().z > start.origin.z + .001f) {
                    auto stepped = start;
                    stepped.origin = up.endpos();
                    bool upperWall = false;
                    if (slide(stepped, delta, upperWall)) {
                        Trace::CGameTrace down{};
                        if (trace(stepped.origin, stepped.origin - Vector_t{0, 0, step + 2.f}, down) &&
                            Trace::DidHit(down) && down.normal().z >= standable) {
                            stepped.origin = down.endpos();
                            stepped.velocity.z = 0.f;
                            const auto a = state.origin - start.origin, b = stepped.origin - start.origin;
                            if (b.x * b.x + b.y * b.y > a.x * a.x + a.y * a.y + .000001f)
                                state = stepped;
                        }
                    }
                }
            }
            // Grounded slope/step motion may have positive Z velocity. Only an
            // explicit takeoff state suppresses the support probe while ascending.
            if (supported || state.velocity.z <= 0.f) {
                Trace::CGameTrace ground{};
                constexpr float contact = .03125f;
                const float reach = supported ? step + 2.f : 2.f;
                const auto from = state.origin + Vector_t{0, 0, supported ? contact : 0.f};
                if (!MovementTrace(from, state.origin - Vector_t{0, 0, reach}, pawn, p.height, ground))
                    return false;
                state.grounded = !ground.startsolid() && Trace::DidHit(ground) && ground.normal().z >= standable;
                if (state.grounded) {
                    state.origin = ground.endpos();
                    state.groundNormal = ground.normal();
                    state.velocity.z =
                        -(state.velocity.x * state.groundNormal.x + state.velocity.y * state.groundNormal.y) /
                        state.groundNormal.z;
                }
            } else
                state.grounded = false;
            return true;
        }

        Vector_t Displacement(MotionState& state,
                              const Vector_t& direction,
                              float goal,
                              bool brake,
                              const Physics& p,
                              bool quantizedDuty = true,
                              float duration = tick,
                              float phase = 0.f) {
            constexpr float wholeSlice = tick / 16.f;
            Vector_t delta{};
            if (state.grounded && state.velocity.x == 0.f && state.velocity.y == 0.f &&
                ((brake && goal == 0.f) || (!brake && direction.Length() <= .001f))) {
                state.velocity.z = 0.f;
                return delta;
            }
            if (duration == tick && phase == 0.f && state.grounded && brake && quantizedDuty && p.friction >= 0.f &&
                p.groundAcceleration >= 0.f) {
                const float speed = std::hypot(state.velocity.x, state.velocity.y);
                const float lower = speed - ((std::max)(speed, p.stop) * p.friction + p.groundAcceleration) * duration;
                if (speed > 0.f && lower > (std::max)(goal, p.stop) + 1.f) {
                    // This entire interval is full counter-velocity input, far from
                    // the ceiling/stop-speed transitions. Its direction is constant;
                    // retain the same 16 discrete friction/acceleration steps without
                    // repeatedly normalizing that vector.
                    const Vector_t heading{state.velocity.x / speed, state.velocity.y / speed, 0};
                    float value = speed, travel = 0.f;
                    for (int i = 0; i < 16; ++i) {
                        value -= value * p.friction * wholeSlice;
                        value -= (std::min)(p.groundAcceleration * wholeSlice, p.wish + value);
                        travel += value * wholeSlice;
                    }
                    state.velocity.x = heading.x * value;
                    state.velocity.y = heading.y * value;
                    const float rise =
                        -(heading.x * state.groundNormal.x + heading.y * state.groundNormal.y) / state.groundNormal.z;
                    state.velocity.z = rise * value;
                    return {heading.x * travel, heading.y * travel, rise * travel};
                }
            }
            // Native CategorizePosition resets airborne friction to 1, then uses
            // 0.25 while rising at <=140u/s. Re-evaluate each predicted tick instead
            // of freezing the factor observed at the start of a jump/braking plan.
            const float airSurface = state.velocity.z > 0.f && state.velocity.z <= 140.f ? .25f : 1.f;
            const float end = phase + duration / tick;
            for (int i = int(std::floor(phase * 16.f)); i < int(std::ceil(end * 16.f)); ++i) {
                const float lo = (std::max)(phase, i / 16.f), hi = (std::min)(end, (i + 1) / 16.f);
                if (hi <= lo)
                    continue;
                const float dt = (hi - lo) * tick;
                const int quanta = std::clamp(int(std::lround((hi - lo) * 64.f)), 1, 4);
                const float speed = std::hypot(state.velocity.x, state.velocity.y);
                if (state.grounded && speed > 0.f) {
                    const float factor = (std::max)(0.f, 1.f - (std::max)(p.stop, speed) * p.friction * dt / speed);
                    state.velocity.x *= factor;
                    state.velocity.y *= factor;
                }
                const float acceleration = state.grounded ? p.groundAcceleration : p.airAcceleration * airSurface,
                            cap = state.grounded ? p.wish : p.airCap;
                if (brake) {
                    const auto pulse = MovementBrake::Solve(state.velocity.x,
                                                            state.velocity.y,
                                                            direction.x * goal,
                                                            direction.y * goal,
                                                            acceleration,
                                                            cap,
                                                            dt,
                                                            quantizedDuty,
                                                            quanta);
                    state.velocity.x += pulse.x * pulse.gain * pulse.duty;
                    state.velocity.y += pulse.y * pulse.gain * pulse.duty;
                } else if (direction.Length() > .001f) {
                    const float gain =
                        (std::min)(acceleration * dt, (std::max)(0.f, cap - Legitbot::Dot(state.velocity, direction)));
                    state.velocity = state.velocity + direction * gain;
                }
                if (state.grounded)
                    state.velocity.z =
                        -(state.velocity.x * state.groundNormal.x + state.velocity.y * state.groundNormal.y) /
                        state.groundNormal.z;
                if (!state.grounded)
                    state.velocity.z -= p.gravity * dt * .5f;
                delta = delta + state.velocity * dt;
                if (!state.grounded)
                    state.velocity.z -= p.gravity * dt * .5f;
            }
            return delta;
        }

        bool Advance(MotionState& state,
                     const Vector_t& direction,
                     float goal,
                     bool brake,
                     C_CSPlayerPawn* pawn,
                     const Physics& p,
                     bool quantizedDuty = true,
                     float duration = tick,
                     float phase = 0.f) {
            const auto delta = Displacement(state, direction, goal, brake, p, quantizedDuty, duration, phase);
            // Native grounded state already supplies support. Re-snapping a motionless
            // rendered pawn to a different collision surface moves its saved hitboxes
            // vertically even though neither pawn nor ground moved.
            if (state.grounded && delta.x == 0.f && delta.y == 0.f && delta.z == 0.f)
                return true;
            return MoveHull(state, delta, pawn, p);
        }

        struct PeekAccuracy {
            C_CSPlayerPawn* local;
            C_CSWeaponBase* weapon;
            CCSWeaponBaseVData* data;
            Physics physics;
            bool scopePlanning;
            int scopeTicks;
            Legitbot::AccuracyState liveAccuracy;
            float penalty;
            bool walking;

            float PenaltyAt(const MotionState& state, int ticks, int mode) const {
                float baseline = Data(data,
                                      (state.grounded && (local->m_fFlags() & FL_DUCKING))
                                          ? "CCSWeaponBaseVData->m_flInaccuracyCrouch"
                                          : "CCSWeaponBaseVData->m_flInaccuracyStand",
                                      mode);
                if (!state.grounded)
                    baseline += Data(data, "CCSWeaponBaseVData->m_flInaccuracyJump", mode) *
                                Cvar::Float("weapon_air_spread_scale", 1.f);
                return LegitMath::RecoverAccuracy(penalty, baseline, liveAccuracy.recovery, (ticks + 1) * tick);
            }

            Legitbot::AccuracyState At(const MotionState& state, int ticks) const {
                auto accuracy = liveAccuracy;
                const bool scoped =
                    accuracy.mode == 1 || ((scopePlanning || weapon->m_zoomLevel() > 0) && ticks >= scopeTicks);
                const int mode = scoped ? 1 : accuracy.mode;
                const float maxSpeed = Data(data, "CCSWeaponBaseVData->m_flMaxSpeed", mode);
                if (!accuracy.valid || maxSpeed <= 0.f) {
                    accuracy.valid = false;
                    return accuracy;
                }
                const float speed = std::hypot(state.velocity.x, state.velocity.y),
                            ratio = std::clamp((speed / maxSpeed - .34f) / .61f, 0.f, 1.f);
                const float moving = Data(data, "CCSWeaponBaseVData->m_flInaccuracyMove", mode) *
                                     (walking ? ratio : std::pow(ratio, .25f));
                const float recovered = PenaltyAt(state, ticks, mode);
                const float scale = Cvar::Float("weapon_air_spread_scale", 1.f);
                const float air =
                    state.grounded
                        ? 0.f
                        : LegitMath::AirInaccuracy(state.velocity.z - physics.gravity * tick,
                                                   Cvar::Float("sv_jump_impulse", 301.99338f),
                                                   Data(data, "CCSWeaponBaseVData->m_flInaccuracyJumpInitial") * scale,
                                                   Data(data, "CCSWeaponBaseVData->m_flInaccuracyJumpApex") * scale);
                accuracy.inaccuracy = (std::max)(0.f,
                                                 accuracy.inaccuracy - accuracy.movement - accuracy.air - penalty +
                                                     moving + air + recovered);
                accuracy.movement = moving;
                accuracy.air = air;
                accuracy.mode = mode;
                accuracy.minimumInaccuracy = Data(data,
                                                  (state.grounded && (local->m_fFlags() & FL_DUCKING))
                                                      ? "CCSWeaponBaseVData->m_flInaccuracyCrouch"
                                                      : "CCSWeaponBaseVData->m_flInaccuracyStand",
                                                  mode);
                if (!state.grounded)
                    accuracy.minimumInaccuracy +=
                        Data(data, "CCSWeaponBaseVData->m_flInaccuracyJump", mode) * scale +
                        LegitMath::AirInaccuracy(0.f,
                                                 Cvar::Float("sv_jump_impulse", 301.99338f),
                                                 Data(data, "CCSWeaponBaseVData->m_flInaccuracyJumpInitial") * scale,
                                                 Data(data, "CCSWeaponBaseVData->m_flInaccuracyJumpApex") * scale);
                if (scoped)
                    accuracy.spread = Data(data, "CCSWeaponBaseVData->m_flSpread", mode);
                return accuracy;
            }
        };

        struct EnemyObservation {
            CBaseHandle handle{};
            Vector_t origin{}, velocity{}, wish{};
            float time = 0.f, duty = 0.f;
            bool grounded = false;
        };

        std::array<EnemyObservation, SdkPrioA::kMaxTrackedControllers> observations{};
        std::size_t observationCursor = 0;

        struct EnemyForecast {
            TargetPose source{};
            C_CSPlayerPawn* pawn = nullptr;
            MotionState state{};
            Physics physics{};
            Vector_t wish{};
            std::array<Vector_t, 129> offsets{};
            int computed = 0;
            bool valid = false, stopping = false;
            std::uint64_t epoch = 0;
        };

        std::array<TargetPose, SdkPrioA::kMaxTrackedControllers> scenePoses{};
        std::array<EnemyForecast, SdkPrioA::kMaxTrackedControllers> forecasts{};
        std::array<EnemyForecast, SdkPrioA::kMaxTrackedControllers> shotForecasts{};
        int shotForecastCount = 0;

        void ObserveEnemy(const TargetPose& pose, EnemyForecast& forecast) {
            auto* pawn = I::GameEntity->Instance->Get<C_CSPlayerPawn>(pose.candidate.handle);
            forecast.source = pose;
            forecast.epoch = hullEpoch;
            forecast.pawn = pawn;
            forecast.computed = 0;
            forecast.offsets[0] = Vector_t{};
            forecast.valid = pawn != nullptr;
            if (!pawn)
                return;
            forecast.state = {pose.origin, pose.velocity, pose.grounded};
            forecast.physics = Parameters(pawn);
            forecast.state.groundNormal = {
                forecast.physics.groundNormal[0], forecast.physics.groundNormal[1], forecast.physics.groundNormal[2]};
            EnemyObservation* previous = nullptr;
            for (auto& record : observations)
                if (record.handle == pose.candidate.handle) {
                    previous = &record;
                    break;
                }
            const float speed = std::hypot(pose.velocity.x, pose.velocity.y);
            Vector_t wish = speed > .01f ? Vector_t{pose.velocity.x / speed, pose.velocity.y / speed, 0} : Vector_t{};
            float duty = pose.grounded && speed > .01f && forecast.physics.groundAcceleration > 0.f
                             ? std::clamp(forecast.physics.friction * (std::max)(forecast.physics.stop, speed) /
                                              forecast.physics.groundAcceleration,
                                          0.f,
                                          1.f)
                             : 0.f;
            if (previous) {
                const float elapsed = pose.simulationTime - previous->time;
                if (elapsed == 0.f && pose.grounded == previous->grounded) {
                    wish = previous->wish;
                    duty = previous->duty;
                } else if (elapsed > 0.f && elapsed <= .25f && (pose.origin - previous->origin).Length() <= 64.f &&
                           pose.grounded == previous->grounded) {
                    auto acceleration = (pose.velocity - previous->velocity) * (1.f / elapsed);
                    acceleration.z = 0;
                    const float oldSpeed = std::hypot(previous->velocity.x, previous->velocity.y);
                    if (pose.grounded && oldSpeed > .01f) {
                        const float drag =
                            forecast.physics.friction * (std::max)(forecast.physics.stop, oldSpeed) / oldSpeed;
                        acceleration.x += previous->velocity.x * drag;
                        acceleration.y += previous->velocity.y * drag;
                    }
                    const float magnitude = acceleration.Length();
                    const float airSurface = previous->velocity.z > 0.f && previous->velocity.z <= 140.f ? .25f : 1.f;
                    const float maximum = pose.grounded ? forecast.physics.groundAcceleration
                                                        : forecast.physics.airAcceleration * airSurface;
                    wish = magnitude > .01f ? acceleration * (1.f / magnitude) : Vector_t{};
                    duty = maximum > 0.f ? std::clamp(magnitude / maximum, 0.f, 1.f) : 0.f;
                }
            } else
                previous = &observations[observationCursor++ % observations.size()];
            if (speed <= .01f) {
                wish = Vector_t{};
                duty = 0.f;
            }
            *previous = {
                pose.candidate.handle, pose.origin, pose.velocity, wish, pose.simulationTime, duty, pose.grounded};
            forecast.stopping = wish.x * pose.velocity.x + wish.y * pose.velocity.y < 0.f &&
                                std::abs(wish.x * pose.velocity.y - wish.y * pose.velocity.x) <= speed * .1f;
            forecast.wish = wish;
            forecast.physics.groundAcceleration *= duty;
            forecast.physics.airAcceleration *= duty;
        }

        bool AdvanceEnemy(EnemyForecast& forecast, int time) {
            if (time < 0 || time >= int(forecast.offsets.size()))
                return false;
            if (forecast.valid && forecast.computed < time && forecast.state.grounded &&
                forecast.state.velocity.x == 0.f && forecast.state.velocity.y == 0.f &&
                forecast.state.velocity.z == 0.f && forecast.wish.x == 0.f && forecast.wish.y == 0.f &&
                forecast.wish.z == 0.f) {
                // Advance preserves this exact grounded, motionless state. Fill its
                // identical offsets without repeating the physics step per tick.
                std::fill(forecast.offsets.begin() + forecast.computed + 1,
                          forecast.offsets.begin() + time + 1,
                          forecast.state.origin - forecast.source.origin);
                forecast.computed = time;
            }
            while (forecast.computed < time && forecast.valid) {
                forecast.valid = Advance(forecast.state,
                                         forecast.wish,
                                         forecast.stopping ? 0.f : forecast.physics.wish,
                                         forecast.stopping,
                                         forecast.pawn,
                                         forecast.physics,
                                         false);
                if (forecast.valid)
                    forecast.offsets[++forecast.computed] = forecast.state.origin - forecast.source.origin;
            }
            return time <= forecast.computed;
        }

    } // namespace

    void BeginEnemyForecasts() {
        shotForecastCount = 0;
        ++hullEpoch;
    }

    bool ForecastEnemyOffset(const TargetPose& pose, int ticks, Vector_t& offset) {
        offset = Vector_t{};
        if (ticks <= 0)
            return ticks == 0;
        if (ticks > 128 || !Bones::IsValidPos(pose.origin) || !std::isfinite(pose.velocity.Length()))
            return false;
        // ObserveEnemy gives a stationary grounded pawn no wish input, and
        // Advance leaves that state unchanged. Avoid reading physics parameters
        // and constructing an identical path for every visual query.
        if (pose.grounded && pose.velocity.x == 0.f && pose.velocity.y == 0.f && pose.velocity.z == 0.f)
            return true;
        const auto matches = [&](const EnemyForecast& candidate) {
            const auto& source = candidate.source;
            return candidate.epoch == hullEpoch && source.candidate.handle == pose.candidate.handle &&
                   source.simulationTime == pose.simulationTime && source.grounded == pose.grounded &&
                   (source.origin - pose.origin).Length() <= .001f &&
                   (source.velocity - pose.velocity).Length() <= .001f;
        };
        // Preparation has often already advanced this enemy along the same path.
        // Share that command-local work with shot and Visualise selection.
        for (auto& prepared : forecasts)
            if (matches(prepared)) {
                if (!AdvanceEnemy(prepared, ticks))
                    return false;
                offset = prepared.offsets[ticks];
                return true;
            }
        EnemyForecast* forecast = nullptr;
        for (int i = 0; i < shotForecastCount; ++i)
            if (shotForecasts[i].source.candidate.handle == pose.candidate.handle) {
                forecast = &shotForecasts[i];
                break;
            }
        if (!forecast) {
            if (shotForecastCount == int(shotForecasts.size()))
                return false;
            forecast = &shotForecasts[shotForecastCount++];
            ObserveEnemy(pose, *forecast);
        } else {
            const auto& previous = forecast->source;
            if (previous.simulationTime != pose.simulationTime || previous.grounded != pose.grounded ||
                (previous.origin - pose.origin).Length() > .001f ||
                (previous.velocity - pose.velocity).Length() > .001f)
                ObserveEnemy(pose, *forecast);
        }
        if (!AdvanceEnemy(*forecast, ticks))
            return false;
        offset = forecast->offsets[ticks];
        return true;
    }

    bool CanAdvanceOnGround(C_CSPlayerPawn* local, const Vector_t& start, const Vector_t& end) {
        const auto delta = end - start;
        if (!local || !(local->m_fFlags() & FL_ONGROUND) || std::hypot(delta.x, delta.y) <= .001f)
            return false;
        // This also serves non-Rage Peek: never reuse a prior command's hull cache.
        ++hullEpoch;
        MotionState state{};
        state.origin = start;
        state.velocity = delta;
        state.grounded = true;
        Physics physics{};
        physics.height = 72.f - 18.f * Duck(local);
        if (!MoveHull(state, delta, local, physics) || !state.grounded)
            return false;
        const auto moved = state.origin - start;
        return moved.x * delta.x + moved.y * delta.y > .000001f;
    }

    float MovementLimit() {
        return limit.load(std::memory_order_relaxed);
    }

    float MovementBrakePhase() {
        return brakePhase.load(std::memory_order_relaxed);
    }

    // 预测当前命令的位移及可选末速度，供开火眼位和精度计算共同使用。
    bool CommandDisplacement(CUserCmd* cmd, C_CSPlayerPawn* local, Vector_t& displacement, Vector_t* finalVelocity) {
        auto* base = cmd ? cmd->csgoUserCmd.pBaseCmd : nullptr;
        auto* service = local ? local->m_pMovementServices() : nullptr;
        auto* node = local ? local->m_pGameSceneNode() : nullptr;
        auto* weapon = local ? local->GetActiveWeapon() : nullptr;
        auto* data = weapon ? weapon->Data() : nullptr;
        if (!base || !base->pViewAngles || !service || !node || !data)
            return false;
        const float maximum = Data(data, "CCSWeaponBaseVData->m_flMaxSpeed", std::clamp(weapon->m_weaponMode(), 0, 1));
        if (maximum <= 0.f)
            return false;
        int count = 0;
        if (base->subtickMovesField.nCurrentSize && !InputInject::SubtickRange(base, count))
            return false;

        struct Step {
            float when = 0, forward = 0, left = 0, yaw = 0;
            bool operator==(const Step&) const = default;
        };

        struct Key {
            C_CSPlayerPawn* pawn = nullptr;
            int command = 0, count = 0;
            bool grounded = false, quantized = false;
            std::array<float, 9> values{};
            Physics physics{};
            std::array<Step, 64> steps{};
            bool operator==(const Key&) const = default;
        } key;

        const auto origin = node->m_vecAbsOrigin(), velocity = local->m_vecVelocity();
        key.pawn = local;
        key.command = cmd->nCommandNumber;
        key.count = count;
        key.grounded = (local->m_fFlags() & FL_ONGROUND) != 0;
        key.quantized = Cvar::Bool("sv_quantize_movement_input", true);
        key.physics = Parameters(local, maximum);
        bool analog = false;
        for (int i = 0; i < count; ++i) {
            const auto* step = base->subtickMovesField.pRep->tElements[i];
            if (!step)
                return false;
            const auto bits = step->nCachedBits;
            const bool axis = step->nButton == 0;
            analog |= axis && (bits & (MOVESTEP_BITS_ANALOG_FORWARD_DELTA | MOVESTEP_BITS_ANALOG_LEFT_DELTA));
            key.steps[i] = {std::clamp(std::nearbyint(step->flWhen * 64.f) / 64.f, 0.f, 1.f),
                            axis && (bits & MOVESTEP_BITS_ANALOG_FORWARD_DELTA) ? step->flAnalogForwardDelta : 0.f,
                            axis && (bits & MOVESTEP_BITS_ANALOG_LEFT_DELTA) ? step->flAnalogLeftDelta : 0.f,
                            axis && (bits & MOVESTEP_BITS_YAW_DELTA) ? step->flYawDelta : 0.f};
        }
        float forward = base->flForwardMove, left = base->flSideMove;
        if (analog && (!Mem::ReadField(service, 0x1a0, forward) || !Mem::ReadField(service, 0x1a4, left)))
            return false;
        key.values = {origin.x,
                      origin.y,
                      origin.z,
                      velocity.x,
                      velocity.y,
                      velocity.z,
                      forward,
                      left,
                      base->pViewAngles->angValue.y};
        static Key previous{};
        static Vector_t previousDisplacement{}, previousVelocity{};
        static bool valid = false;
        if (valid && previous == key) {
            displacement = previousDisplacement;
            if (finalVelocity)
                *finalVelocity = previousVelocity;
            return true;
        }
        auto steps = key.steps;
        std::stable_sort(steps.begin(), steps.begin() + count, [](const auto& a, const auto& b) {
            return a.when < b.when;
        });
        MotionState state{origin,
                          velocity,
                          key.grounded,
                          {key.physics.groundNormal[0], key.physics.groundNormal[1], key.physics.groundNormal[2]}};
        float yaw = base->pViewAngles->angValue.y, phase = 0.f;
        int next = 0;
        while (phase < 1.f) {
            while (next < count && steps[next].when <= phase) {
                const auto& step = steps[next++];
                forward += step.forward;
                left += step.left;
                yaw += step.yaw;
            }
            const float end = next < count ? steps[next].when : 1.f;
            if (end <= phase)
                continue;
            float f = key.quantized ? std::round(forward) : forward, l = key.quantized ? std::round(left) : left;
            const float length = std::hypot(f, l);
            Vector_t direction{};
            auto physics = key.physics;
            if (length > 0.f) {
                f /= length;
                l /= length;
                const float angle = yaw * rad;
                direction = {std::cos(angle) * f - std::sin(angle) * l, std::sin(angle) * f + std::cos(angle) * l, 0};
                const float scale = (std::min)(1.f, length);
                physics.wish *= scale;
                physics.groundAcceleration *= scale;
                physics.airAcceleration *= scale;
                physics.airCap = (std::min)(physics.airCap, physics.wish);
            } else
                physics.wish = physics.groundAcceleration = physics.airAcceleration = physics.airCap = 0.f;
            if (!Advance(state, direction, physics.wish, false, local, physics, false, (end - phase) * tick, phase))
                return false;
            phase = end;
        }
        // Charged fire falls outside the native shoot-position ring and uses the
        // final movement position. Never run native prediction a second time.
        displacement = state.origin - origin;
        previous = key;
        previousDisplacement = displacement;
        previousVelocity = state.velocity;
        if (finalVelocity)
            *finalVelocity = state.velocity;
        valid = true;
        return true;
    }

    void ResetMotion() {
        limit.store(-1.f);
        brakePhase.store(0.f);
        braking = false;
        brakingTarget = {};
        observations = {};
        observationCursor = 0;
        shotForecastCount = 0;
        preparationDamageCursors = {};
        preparationReadyCursors = {};
        ++hullEpoch;
    }

    void PrepareMotion(CUserCmd* cmd,
                       C_CSPlayerPawn* local,
                       C_CSWeaponBase* weapon,
                       Candidate& target,
                       const Settings& c,
                       bool intent) {
        limit.store(-1.f);
        brakePhase.store(0.f);
        ++hullEpoch;
        auto* base = cmd->csgoUserCmd.pBaseCmd;
        auto* data = weapon ? weapon->Data() : nullptr;
        if (!base || !base->pViewAngles || !data)
            return;
        const float maximum = Data(data, "CCSWeaponBaseVData->m_flMaxSpeed", std::clamp(weapon->m_weaponMode(), 0, 1));
        if (maximum <= 0)
            return;
        // Preparation follows the attack cooldown below; the reload animation
        // flag can outlive the actual reload and must not suppress scope/stop.
        if (!intent || c.force_no_spread || (!c.auto_stop && !c.auto_scope) || weapon->m_iClip1() <= 0) {
            braking = false;
            brakingTarget = {};
            return;
        }
        const auto velocity = local->m_vecVelocity();
        const float speed = std::hypot(velocity.x, velocity.y);
        const bool grounded = (local->m_fFlags() & FL_ONGROUND) != 0;
        const bool takeoff = grounded && (cmd->nButtons.nValue & IN_JUMP);
        const bool stopping = !c.force_no_spread && c.auto_stop && ((grounded && !takeoff) || c.air_stop);
        auto direction = Direction(base);
        const bool movingInput = direction.Length() > .01f;
        const bool manualWalk = Config::movement.slowwalk && grounded;
        const float manualCap = maximum * .34f * std::clamp(Config::movement.slowwalk_speed, 0.f, 100.f) * .01f;
        const float floor = c.stop_mode == 0 || (grounded && !movingInput) ? 0.f
                            : manualWalk                                   ? manualCap
                                                                           : maximum * .34f;
        const auto physics = Parameters(local, maximum);
        if (!grounded)
            direction = speed > .001f ? Vector_t{velocity.x / speed, velocity.y / speed, 0} : Vector_t{};
        const auto origin = local->m_pGameSceneNode()->m_vecAbsOrigin();
        // The planning position belongs to the pre-movement pawn, not an older
        // shoot-position history entry. Keep its current eye offset on every path.
        const auto eye = origin + local->m_vecViewOffset();
        auto* controller = I::GameEntity->Instance->Get<CCSPlayerController>(local->m_hController());
        // Prepare against this command's weapon clock. A previous submission's
        // offset can be stale after correction/reload and delay preparation.
        double now = base->nClientTick > 0 ? ForecastShotClock(base->nClientTick)
                     : controller          ? double(controller->m_nTickBase())
                                           : 0.;
        const bool nativeClock = Legitbot::ReadShotClock(cmd, weapon, now);
        if (nativeClock)
            ObserveShotClock(base->nClientTick, now);
        const double next = double(weapon->m_nNextPrimaryAttackTick()) + weapon->m_flNextPrimaryAttackTickRatio();
        const int cooldown = controller ? (std::max)(0, int(std::ceil(next - now))) : 64;
        const int definition = weapon->m_iItemDefinitionIndex();
        const bool canScope = data->m_WeaponType() == 5 || definition == 8 || definition == 39;
        const bool scopePlanning =
            !c.force_no_spread && c.auto_scope && canScope && !local->m_bIsScoped() && weapon->m_zoomLevel() == 0;
        if (!scopePlanning && (!stopping || (!movingInput && speed <= floor + .5f))) {
            braking = false;
            brakingTarget = {};
            return;
        }
        const int scopeTicks = (std::max)(0, int(std::ceil(Data(data, "CCSWeaponBaseVData->m_flZoomTime0") / tick)));
        const int scopeLead = scopeTicks + c.scope_early;
        constexpr int scopeStart = 0;
        const int horizon = std::clamp(scopePlanning ? (std::max)(32, scopeLead) : 32, 1, 64);
        const auto liveAccuracy = Legitbot::ReadAccuracy(weapon);
        const float penalty = weapon->m_fAccuracyPenalty();
        const auto walkOffset = SchemaFinder::Get(hash_32_fnv1a_const("C_CSPlayerPawn->m_bIsWalking"));
        const bool walking =
            walkOffset && *reinterpret_cast<bool*>(reinterpret_cast<std::uintptr_t>(local) + walkOffset);
        const PeekAccuracy projectedAccuracy{
            local, weapon, data, physics, scopePlanning, scopeTicks, liveAccuracy, penalty, walking};
        const auto accuracyAt = [&](const MotionState& state, int ticks) {
            return projectedAccuracy.At(state, ticks);
        };
        const auto preferred = target.handle;
        const int enemyCount =
            CollectTargetPoses(local,
                               weapon,
                               eye,
                               base->pViewAngles->angValue,
                               c,
                               scenePoses,
                               ((std::max)(speed, physics.wish) + Cvar::Float("sv_maxspeed", 320.f)) * horizon * tick);
        if (!enemyCount) {
            target = {};
            braking = false;
            brakingTarget = {};
            return;
        }
        for (int i = 0; i < enemyCount; ++i)
            ObserveEnemy(scenePoses[i], forecasts[i]);
        const bool stationaryTargets =
            !c.movement_extrapolation ||
            std::all_of(forecasts.begin(), forecasts.begin() + enemyCount, [](const auto& enemy) {
                const auto& v = enemy.source.velocity;
                return enemy.source.grounded && v.x == 0.f && v.y == 0.f && v.z == 0.f;
            });
        ScanBudget forecastBudget{std::chrono::steady_clock::now() + std::chrono::microseconds(500)};
        ScanBudget* activeBudget = &forecastBudget;
        bool budgetStarted = false;
        const auto speculate = [&] {
            if (!budgetStarted) {
                forecastBudget.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2);
                budgetStarted = true;
            }
            activeBudget = &forecastBudget;
        };
        const auto canDamage = [&](const MotionState& state,
                                   int time,
                                   bool checkAccuracy = false,
                                   bool selectBest = false,
                                   Candidate* potential = nullptr,
                                   const Candidate* verified = nullptr) -> Candidate {
            const auto at = eye + state.origin - origin;
            Candidate best = verified ? *verified : Candidate{};
            struct Check {
                C_CSPlayerPawn* local;
                C_CSWeaponBase* weapon;
                const Settings* settings;
                Vector_t eye;
                Legitbot::AccuracyState accuracy;
                Candidate* best;
                CBaseHandle retained;
            } check{local, weapon, &c, at, {}, &best, preferred};
            ScanAcceptance acceptance{};
            if (checkAccuracy && !c.SeedGate() && !c.force_no_spread) {
                check.accuracy = accuracyAt(state, time);
                if (!check.accuracy.valid)
                    return {};
                acceptance = {[](void* context, const Candidate& candidate) {
                                  auto& q = *static_cast<Check*>(context);
                                  if (*q.best && !Prefer(candidate, *q.best, *q.settings, q.retained))
                                      return false;
                                  // Complete this point's unchanged HC test. The next point
                                  // observes the deadline; restarting a partial spread test on
                                  // every command would permanently starve an expensive point.
                                  return ProjectedHitchance(q.local,
                                                            q.weapon,
                                                            candidate,
                                                            *q.settings,
                                                            q.eye,
                                                            AimAngle(q.eye, candidate.point),
                                                            q.accuracy,
                                                            nullptr);
                              },
                              &check};
            }
            acceptance.budget = activeBudget;
            auto pointSettings = c;
            if (c.AutoMultipoint()) {
                const auto pointAccuracy =
                    checkAccuracy && check.accuracy.valid ? check.accuracy : accuracyAt(state, time);
                pointSettings.auto_point_cone = AutoPointCone(pointAccuracy, c.auto_multipoint_mode, c.hc_budget);
            }
            auto& traversal =
                (checkAccuracy ? preparationReadyCursors : preparationDamageCursors)[std::clamp(time, 0, 64)];
            // A blocked enemy's MP edges must not consume the whole slice before
            // another enemy's center is visited, including at future endpoints.
            // Keep separate scheduling cursors; all damage/HC results stay fresh.
            const int passes = selectBest ? 1 : 2;
            for (int pass = 0; pass < passes; ++pass) {
                const bool centerPass = !selectBest && pass == 0;
                auto& cursor = centerPass ? traversal.centers : traversal.edges;
                const auto& settings = pointSettings;
                acceptance.firstPoint = !selectBest && !centerPass ? 1 : 0;
                acceptance.lastPoint = centerPass ? 1 : 5;
                const int first = selectBest ? 0 : std::clamp(cursor.target, 0, enemyCount - 1);
                for (int i = first; i < enemyCount; ++i) {
                    if (!selectBest)
                        cursor.target = i;
                    if (activeBudget && activeBudget->Expired())
                        return best;
                    auto& enemy = forecasts[i];
                    const int enemyTime = c.movement_extrapolation ? time : 0;
                    if (!AdvanceEnemy(enemy, enemyTime))
                        continue;
                    const auto offset = enemy.offsets[enemyTime];
                    auto& projected = enemy.source.candidate;
                    const auto center = enemy.source.origin + offset;
                    projected.distance = (center - at).Length();
                    if (best) {
                        if (c.priority == 2 && projected.health > best.health)
                            continue;
                        if (c.priority == 3 && projected.distance > best.distance + .001f)
                            continue;
                        if (c.priority == 4 && projected.distance < best.distance - .001f)
                            continue;
                    }
                    acceptance.damageFloor =
                        best && c.priority == 1 && liveAccuracy.bullets == 1 ? best.damage - .01f : 0.f;
                    auto candidate = ScanCandidate(local,
                                                   weapon,
                                                   projected,
                                                   at,
                                                   base->pViewAngles->angValue,
                                                   settings,
                                                   !selectBest,
                                                   potential != nullptr,
                                                   acceptance,
                                                   offset,
                                                   selectBest ? nullptr : &cursor);
                    if (activeBudget && activeBudget->exhausted)
                        return best;
                    if (!candidate)
                        continue;
                    if (!candidate.shootable) {
                        if (potential && Prefer(candidate, *potential, c, preferred))
                            *potential = candidate;
                        continue;
                    }
                    if (!selectBest)
                        return candidate;
                    if (Prefer(candidate, best, c, preferred))
                        best = candidate;
                }
                if (!selectBest)
                    cursor = {};
            }
            return best;
        };
        const auto queryHash = [](const auto& values) {
            std::uint32_t hash = 2166136261u;
            for (float value : values)
                hash = (hash ^ std::bit_cast<std::uint32_t>(value)) * 16777619u;
            hash ^= hash >> 16;
            hash *= 0x7feb352du;
            hash ^= hash >> 15;
            hash *= 0x846ca68bu;
            return hash ^ (hash >> 16);
        };

        struct DamageQuery {
            std::array<float, 4> key{};
            std::uint64_t epoch = 0;
            bool result = false;
        };

        static std::array<DamageQuery, 4096> damageQueries{};
        const auto hasDamage = [&](const MotionState& state, int time) {
            if (activeBudget && activeBudget->Expired())
                return false;
            const std::array<float, 4> key{
                state.origin.x, state.origin.y, state.origin.z, stationaryTargets ? 0.f : float(time)};
            auto& cached = damageQueries[queryHash(key) & (damageQueries.size() - 1)];
            if (cached.epoch == hullEpoch && cached.key == key)
                return cached.result;
            if (canDamage(state, time)) {
                cached = {key, hullEpoch, true};
                return true;
            }
            if (!activeBudget || !activeBudget->exhausted)
                cached = {key, hullEpoch, false};
            return false;
        };
        MotionState initial{origin, velocity, grounded};
        initial.groundNormal = {physics.groundNormal[0], physics.groundNormal[1], physics.groundNormal[2]};
        if (takeoff) {
            initial.grounded = false;
            initial.velocity.z = Cvar::Float("sv_jump_impulse", 301.99338f);
        }
        const float scopedMaximum = Data(data, "CCSWeaponBaseVData->m_flMaxSpeed", 1);
        const float scopedRatio = scopedMaximum > 0.f ? scopedMaximum / maximum : 1.f;
        const auto goalAt = [&](int time) {
            return floor * (scopePlanning && time > scopeStart ? scopedRatio : 1.f);
        };
        const float movementScale = (std::min)(1.f, std::hypot(base->flForwardMove, base->flSideMove));
        const auto advanceControl = [&](MotionState& state,
                                        int time,
                                        bool brake,
                                        float duration = tick,
                                        float phase = 0.f,
                                        float ceiling = -1.f) {
            if (activeBudget && activeBudget->Expired())
                return false;
            auto parameters = physics;
            if (scopePlanning && time > scopeStart) {
                parameters.wish *= scopedRatio;
                parameters.groundAcceleration *= scopedRatio;
                parameters.airAcceleration *= scopedRatio;
                parameters.airCap = (std::min)(parameters.airCap, parameters.wish);
            }
            const bool apply = brake && (state.grounded || c.air_stop);
            if (!apply && !manualWalk) {
                parameters.wish *= movementScale;
                parameters.groundAcceleration *= movementScale;
                parameters.airAcceleration *= movementScale;
                parameters.airCap = (std::min)(parameters.airCap, parameters.wish);
            }
            const auto wish = state.grounded ? direction
                                             : (std::hypot(state.velocity.x, state.velocity.y) > .001f
                                                    ? Vector_t{state.velocity.x, state.velocity.y, 0}.Normalize()
                                                    : direction);
            return Advance(state,
                           wish,
                           apply                          ? (ceiling >= 0.f ? ceiling : goalAt(time))
                           : manualWalk && state.grounded ? manualCap
                                                          : maximum,
                           apply || (manualWalk && state.grounded),
                           local,
                           parameters,
                           apply,
                           duration,
                           phase);
        };
        const auto advancePlan = [&](MotionState& state, int time, bool brake, int start) {
            // The first braking endpoint can be as expensive as any later one.
            // Resume its point traversal instead of exempting it from the budget.
            speculate();
            return advanceControl(state, time, brake);
        };

        struct ReadyQuery {
            std::array<float, 7> key{};
            std::uint64_t epoch = 0;
            Candidate result{};
        };

        static std::array<ReadyQuery, 512> readyQueries{};
        const auto readyAt = [&](const MotionState& state, int time) -> Candidate {
            if (scopePlanning && time < scopeStart + scopeTicks)
                return {};
            if (stopping &&
                std::hypot(state.velocity.x, state.velocity.y) > goalAt(time) + (c.stop_mode == 0 ? 1.f : .5f))
                return {};
            if (!hasDamage(state, time))
                return {};
            // All admitted scope schedules are already zoomed at this endpoint.
            // Their identical position, speed, time and ground state imply the
            // same HC query; do not repeat its spread/penetration rays.
            const std::array<float, 7> key{state.origin.x,
                                           state.origin.y,
                                           state.origin.z,
                                           std::hypot(state.velocity.x, state.velocity.y),
                                           state.velocity.z,
                                           float(time),
                                           float(state.grounded)};
            auto& cached = readyQueries[queryHash(key) & (readyQueries.size() - 1)];
            Candidate hit{};
            if (cached.epoch == hullEpoch && cached.key == key)
                hit = cached.result;
            else {
                hit = canDamage(state, time, true);
                if (!activeBudget || !activeBudget->exhausted)
                    cached = {key, hullEpoch, hit};
            }
            return hit;
        };
        const auto atRest = [&](const MotionState& state, int time) {
            return state.grounded &&
                   std::hypot(state.velocity.x, state.velocity.y) <= goalAt(time) + (c.stop_mode == 0 ? 1.f : .5f);
        };
        const auto deferDamage = [&](const MotionState& state, int time) {
            return !atRest(state, time);
        };
        const auto distance = [](const MotionState& a, const MotionState& b) {
            return std::hypot(a.origin.x - b.origin.x, a.origin.y - b.origin.y);
        };
        Candidate currentPotential{};
        const auto currentHit = canDamage(initial, 0, false, false, &currentPotential);
        const bool damageNow = bool(currentHit);
        const std::array<float, 4> currentKey{initial.origin.x, initial.origin.y, initial.origin.z, 0.f};
        if (!forecastBudget.exhausted)
            damageQueries[queryHash(currentKey) & (damageQueries.size() - 1)] = {currentKey, hullEpoch, damageNow};
        forecastBudget.exhausted = false;
        speculate();
        const bool controls = stopping && (movingInput || speed > floor + .5f);
        ShotPlan<MotionState, Candidate> plan;
        const bool exposedReady = damageNow && cooldown == 0;
        // The opening has already arrived: maximum braking/scope must begin now.
        // Searching future scope/brake permutations cannot improve that decision.
        if (exposedReady)
            plan.brakeStart = controls ? 0 : -1;
        else if (controls)
            plan = PlanShot<MotionState, Candidate>(initial,
                                                    horizon,
                                                    cooldown,
                                                    controls,
                                                    damageNow,
                                                    c.stop_early,
                                                    controls,
                                                    advancePlan,
                                                    readyAt,
                                                    hasDamage,
                                                    atRest,
                                                    distance,
                                                    deferDamage);
        float refinedPhase = 0.f;
        if (controls && grounded && !takeoff && !manualWalk && plan.brakeStart == 1 && !forecastBudget.exhausted) {
            speculate();
            // The next command is too coarse at a wall edge. Find the first native
            // 1/64-command phase that still reaches the same feasible fire window.
            for (int quantum = 1; quantum < 64; ++quantum) {
                if (forecastBudget.Expired())
                    break;
                const float phase = quantum / 64.f;
                auto state = initial;
                if (!advanceControl(state, 0, false, phase * tick, 0.f) ||
                    !advanceControl(state, 0, true, (1.f - phase) * tick, phase))
                    continue;
                std::array<MotionState, 65> path{};
                path[0] = initial;
                for (int time = 1; time <= plan.fireTick; ++time) {
                    path[time] = state;
                    // A too-fast prefix cannot fire. Test its MD opening only
                    // after the braking endpoint passes, not once per phase.
                    if (atRest(state, time) && !hasDamage(state, time))
                        break;
                    if (time >= cooldown)
                        if (auto hit = readyAt(state, time)) {
                            auto openingState = initial;
                            if (!damageNow) {
                                openingState = state;
                                for (int i = 1; i < time; ++i)
                                    if (hasDamage(path[i], i)) {
                                        openingState = path[i];
                                        break;
                                    }
                            }
                            plan = {0, time, distance(openingState, state), state, hit};
                            refinedPhase = phase;
                            break;
                        }
                    if (time == plan.fireTick || !advanceControl(state, time, true))
                        break;
                }
                if (refinedPhase > 0.f)
                    break;
            }
        }
        if (plan.hit && preferred.valid() && !forecastBudget.exhausted) {
            // This endpoint already passed MD and HC. Re-ranking only needs to
            // validate candidates that can improve on that completed result.
            if (auto ranked = canDamage(plan.state, plan.fireTick, true, true, nullptr, &plan.hit))
                plan.hit = ranked;
        }
        ScanBudget earlyBudget{std::chrono::steady_clock::now() + std::chrono::microseconds(500)};
        activeBudget = &earlyBudget;
        // Early is extra preparation lead, not merely a tie-break between
        // otherwise identical schedules. Fullstop approaches at the accuracy
        // ceiling until its final stop can reach MD; it must not stop in cover.
        Candidate preparationHit{};
        int preparationTime = 0;
        const float preparationCap = manualWalk ? manualCap : maximum * .34f;
        if (intent && controls && grounded && !takeoff && movingInput && c.stop_early > 0 && plan.brakeStart != 0 &&
            preparationCap > .5f) {
            auto state = initial;
            bool valid = true;
            int time = 0;
            const auto capAt = [&](int at) {
                return preparationCap * (scopePlanning && at > scopeStart ? scopedRatio : 1.f);
            };
            for (; time < 32 && std::hypot(state.velocity.x, state.velocity.y) > capAt(time) + .5f; ++time)
                if (!advanceControl(state, time, true, tick, 0.f, capAt(time))) {
                    valid = false;
                    break;
                }
            if (time == 32)
                valid = false;
            for (int i = 0; valid && i < c.stop_early; ++i, ++time)
                valid = advanceControl(state, time, true, tick, 0.f, capAt(time));
            if (c.stop_mode == 0)
                for (int i = 0; valid && i < 32 && std::hypot(state.velocity.x, state.velocity.y) > 1.f; ++i, ++time)
                    valid = advanceControl(state, time, true);
            if (c.stop_mode == 0)
                valid &= std::hypot(state.velocity.x, state.velocity.y) <= 1.f;
            if (valid && time < int(forecasts[0].offsets.size()) && cooldown <= time) {
                preparationHit = canDamage(state, time, true);
                if (preparationHit)
                    preparationTime = time;
            }
        }
        bool scopeSoon = false;
        static int scopeCursor = 64;
        if (!scopePlanning)
            scopeCursor = 64;
        if (intent && scopePlanning && !damageNow && plan.brakeStart != 0 && cooldown <= scopeLead) {
            const int lead = (std::min)(scopeLead, 64);
            std::array<MotionState, 65> path{};
            path[0] = initial;
            int count = 1;
            for (int time = 0; time < lead; ++time) {
                auto state = path[time];
                const float cap = preparationHit ? preparationCap * (time > scopeStart ? scopedRatio : 1.f) : -1.f;
                if (!advanceControl(state, time, bool(preparationHit), tick, 0.f, cap))
                    break;
                path[count++] = state;
            }
            // A blocked window can contain thousands of penetration rays. Finish
            // each MD sample, then continue the window on the next command rather
            // than multiplying that worst-case cost by every forecast tick here.
            // The cursor only orders fresh queries: never reuse old cover results
            // after either player or the scene has moved, and never starve a tick
            // by restarting the same expensive failed prefix every command.
            if (count > 1) {
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2);
                int time = std::clamp(scopeCursor, 1, count - 1);
                for (int visited = 0; visited < count - 1; ++visited) {
                    scopeSoon = hasDamage(path[time], time);
                    time = time > 1 ? time - 1 : count - 1;
                    if (scopeSoon || std::chrono::steady_clock::now() >= deadline)
                        break;
                }
                scopeCursor = scopeSoon ? 64 : time;
            }
        }
        const auto chosen = plan.hit ? plan.hit : currentHit ? currentHit : preparationHit;
        if (chosen) {
            int targetIndex = 0;
            for (int i = 0; i < enemyCount; ++i)
                if (forecasts[i].source.candidate.handle == chosen.handle) {
                    targetIndex = i;
                    break;
                }
            const int at = plan.hit ? plan.fireTick : currentHit ? 0 : preparationTime;
            const auto offset = forecasts[targetIndex].offsets[c.movement_extrapolation ? at : 0];
            target = forecasts[targetIndex].source.candidate;
            target.group = chosen.group;
            target.studio = chosen.studio;
            target.pointIndex = chosen.pointIndex;
            target.point = chosen.point - offset;
            target.damage = chosen.damage;
            target.angle = chosen.angle;
            target.shootable = currentHit && currentHit.handle == chosen.handle;
        } else if (!target)
            target = currentPotential;
        if (intent && controls && (plan.brakeStart == 0 || preparationHit)) {
            if (!braking || brakingTarget != target.handle) {
                initialSpeed = speed;
                brakingTarget = target.handle;
            }
            braking = true;
            limit.store(plan.brakeStart == 0 ? goalAt(0) : preparationCap);
            brakePhase.store(refinedPhase);
        } else {
            braking = false;
            brakingTarget = {};
        }
        if (intent && scopePlanning && (scopeSoon || damageNow || (plan.brakeStart == 0 && plan.fireTick >= 0)))
            InputInject::SetEdge(cmd, IN_SECOND_ATTACK, true);
    }

    // 沿左右移动路径评估可开火目标；结果交给 Visualise 绘制。
    PeekTargets ForecastPeek(CUserCmd* cmd,
                             C_CSPlayerPawn* local,
                             C_CSWeaponBase* weapon,
                             const Settings& c,
                             const QAngle_t& view,
                             CBaseHandle retained,
                             const Submission& submitted) {
        PeekTargets result{};
        auto* data = weapon ? weapon->Data() : nullptr;
        auto* node = local ? local->m_pGameSceneNode() : nullptr;
        if (!cmd || !node || !data || weapon->IsNonGunWeapon() || weapon->m_iClip1() <= 0)
            return result;
        const auto dt = GetDoubleTapStatus();
        const bool pair = c.double_tap && weapon->m_iItemDefinitionIndex() != 64 && weapon->m_iClip1() >= 2 &&
                          (dt.state == DoubleTapState::Ready ||
                           (submitted.fired && DoubleTapReservedLethal(submitted.target.handle)));
        bool firstLethal = submitted.fired && DoubleTapReservedLethal(submitted.target.handle);
        if (submitted.fired) {
            result.first = submitted.target;
            if (!pair)
                return result;
        }
        const auto origin = node->m_vecAbsOrigin(), eye = Bones::GetEyePos(local);
        // No movement or spread simulation is useful without an eligible pawn.
        const int sceneCount = CollectTargetPoses(local, weapon, eye, view, c, scenePoses, 50.f);
        if (!sceneCount)
            return result;
        const float maximum = Data(data, "CCSWeaponBaseVData->m_flMaxSpeed", std::clamp(weapon->m_weaponMode(), 0, 1));
        if (maximum <= 0.f)
            return result;
        const auto physics = Parameters(local, maximum);
        const int definition = weapon->m_iItemDefinitionIndex();
        const bool scope = c.auto_scope && !c.force_no_spread &&
                           (data->m_WeaponType() == 5 || definition == 8 || definition == 39) && !local->m_bIsScoped();
        const int scopeTicks =
            scope ? (std::max)(0, int(std::ceil(Data(data, "CCSWeaponBaseVData->m_flZoomTime0") / tick))) : 0;
        const auto walkingOffset = SchemaFinder::Get(hash_32_fnv1a_const("C_CSPlayerPawn->m_bIsWalking"));
        const bool walking =
            walkingOffset && *reinterpret_cast<bool*>(reinterpret_cast<std::uintptr_t>(local) + walkingOffset);
        const PeekAccuracy accuracy{local,
                                    weapon,
                                    data,
                                    physics,
                                    scope,
                                    scopeTicks,
                                    Legitbot::ReadAccuracy(weapon),
                                    weapon->m_fAccuracyPenalty(),
                                    walking};
        if (!accuracy.liveAccuracy.valid)
            return result;
        ProjectionContext projection;
        if (!PrepareProjection(cmd, local, weapon, c, projection))
            return result;
        const double clock = projection.shotTime;
        const double next = double(weapon->m_nNextPrimaryAttackTick()) + weapon->m_flNextPrimaryAttackTickRatio();
        const int cooldown = (std::max)(0, int(std::ceil(next - clock)));
        MotionState initial{origin,
                            local->m_vecVelocity(),
                            (local->m_fFlags() & FL_ONGROUND) != 0,
                            {physics.groundNormal[0], physics.groundNormal[1], physics.groundNormal[2]}};

        // Keep scheduling progress, not old collision/HC answers. Every displayed
        // target is selected again from this command's eye, movement and live pose.
        // Resetting the cursor on camera/movement changes starves wall peeks.
        struct Sweep {
            int step = 0, side = -1, hintStep = -1, hintSide = 0;
            Vector_t hintOrigin{};
            bool recover = true;
            std::array<std::array<ScanCursor, 129>, 2> queries{};
        };

        struct Search {
            CBaseHandle weapon{};
            Sweep first{}, second{};
            CBaseHandle firstTarget{};
        };

        static Search search;
        const auto weaponHandle = reinterpret_cast<CEntityInstance*>(weapon)->handle();
        if (search.weapon != weaponHandle) {
            search = {};
            search.weapon = weaponHandle;
        }
        const float yaw = view.y * rad;
        const Vector_t left{-std::sin(yaw), std::cos(yaw), 0};
        const float scopedMaximum = Data(data, "CCSWeaponBaseVData->m_flMaxSpeed", 1);
        const auto parametersAt = [&](int time) {
            auto p = physics;
            if (scope && time >= scopeTicks && scopedMaximum > 0.f) {
                const float ratio = scopedMaximum / maximum;
                p.wish *= ratio;
                p.groundAcceleration *= ratio;
                p.airAcceleration *= ratio;
                p.airCap = (std::min)(p.airCap, p.wish);
            }
            return p;
        };

        struct Endpoint {
            MotionState state{};
            int time = 0, step = 0, side = 0;
            ProjectedShot shot{};
        };

        const auto scan = [&](Sweep& sweep,
                              const MotionState& start,
                              int startTime,
                              int readyTime,
                              int window,
                              int onlySide,
                              CBaseHandle excluded,
                              const PeekAccuracy& prediction,
                              bool second,
                              Endpoint seed) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1);
            struct Path {
                std::array<MotionState, 129> states{};
                int count = 1;
                bool ended = false;
            };
            std::array<Path, 2> paths;
            for (auto& path : paths)
                path.states[0] = start;
            Endpoint best = seed;
            const auto endpoint = [&](int step, int side, Endpoint& out) {
                if (step < 0 || step > 128 || step > window)
                    return false;
                const auto direction = left * float(side);
                auto& path = paths[side > 0];
                while (path.count <= step && !path.ended) {
                    auto state = path.states[path.count - 1];
                    const auto before = state.origin;
                    const int time = startTime + path.count - 1;
                    const auto p = parametersAt(time);
                    const float cap = scope && time >= scopeTicks && scopedMaximum > 0.f ? scopedMaximum : maximum;
                    if (!Advance(state, direction, cap, false, local, p)) {
                        path.ended = true;
                        break;
                    }
                    const auto delta = state.origin - origin;
                    if (std::hypot(delta.x, delta.y) > 50.001f || (state.origin - before).Length() < .001f) {
                        path.ended = true;
                        break;
                    }
                    path.states[path.count++] = state;
                }
                if (step >= path.count)
                    return false;
                auto state = path.states[step];
                int time = startTime + step;
                const bool stop = c.auto_stop && !c.force_no_spread && (state.grounded || c.air_stop);
                for (int waited = 0;; ++waited) {
                    const float cap = scope && time >= scopeTicks && scopedMaximum > 0.f ? scopedMaximum : maximum;
                    const float floor = c.stop_mode == 0 ? 0.f : cap * .34f;
                    if (time >= readyTime && time >= scopeTicks &&
                        (!stop ||
                         std::hypot(state.velocity.x, state.velocity.y) <= floor + (c.stop_mode == 0 ? 1.f : .5f)))
                        break;
                    if (waited >= 64 || time >= startTime + window ||
                        !Advance(state, direction, stop ? floor : cap, stop, local, parametersAt(time)))
                        return false;
                    ++time;
                    const auto delta = state.origin - origin;
                    if (std::hypot(delta.x, delta.y) > 50.001f)
                        return false;
                }
                if (time > startTime + window)
                    return false;
                out = {state, time, step, side, {}};
                return true;
            };
            // Several braking schedules reach exactly the same endpoint. Share
            // that selection only inside this command, where its scene is fixed.
            struct Checked {
                Vector_t eye;
                Legitbot::AccuracyState accuracy;
                int time;
                ProjectedShot shot;
                bool complete;
            };
            std::array<Checked, 8> checked{};
            int checkedCount = 0;
            const auto evaluate = [&](Endpoint& point, bool searching = false) {
                if (best.shot.target && point.time > best.time)
                    return true;
                const auto at = eye + point.state.origin - origin;
                const auto spread = prediction.At(point.state, point.time - startTime);
                for (int i = 0; i < checkedCount; ++i) {
                    const auto& q = checked[i];
                    if (q.eye.x == at.x && q.eye.y == at.y && q.eye.z == at.z && q.time == point.time &&
                        q.accuracy.inaccuracy == spread.inaccuracy && q.accuracy.spread == spread.spread &&
                        q.accuracy.mode == spread.mode) {
                        point.shot = q.shot;
                        return q.complete;
                    }
                }
                // Negative wall queries may contain hundreds of penetration rays.
                // Resume their point order instead of finishing every current/hint
                // query without a budget. Keep a share for the other locations.
                const auto now = std::chrono::steady_clock::now();
                ScanBudget slice{searching ? deadline : (std::min)(deadline, now + std::chrono::microseconds(250))};
                bool complete = false;
                if (!slice.Expired()) {
                    auto& cursor = sweep.queries[point.side > 0][point.step];
                    const auto probe = SelectProjectedGun(cmd,
                                                          local,
                                                          weapon,
                                                          c,
                                                          projection,
                                                          view,
                                                          excluded.valid() ? excluded : retained,
                                                          at,
                                                          spread,
                                                          point.time,
                                                          excluded,
                                                          &slice,
                                                          second,
                                                          &cursor);
                    complete = !slice.exhausted;
                    if (probe.target) {
                        // Only a fully fresh, ordinary selection may be published.
                        // The preflight never substitutes its first accepted point
                        // for the real MD/HC and target/hitbox ranking.
                        point.shot = SelectProjectedGun(cmd,
                                                        local,
                                                        weapon,
                                                        c,
                                                        projection,
                                                        view,
                                                        excluded.valid() ? excluded : retained,
                                                        at,
                                                        spread,
                                                        point.time,
                                                        excluded,
                                                        nullptr,
                                                        second);
                        complete = true;
                    }
                }
                if (checkedCount < int(checked.size()))
                    checked[checkedCount++] = {at, spread, point.time, point.shot, complete};
                return complete;
            };
            const auto consider = [&](const Endpoint& point) {
                if (point.shot.target &&
                    (!best.shot.target || point.time < best.time ||
                     (point.time == best.time && Prefer(point.shot.target, best.shot.target, c, retained))))
                    best = point;
            };
            // Moving towards an opening can make it shootable after braking now.
            // Always rank that endpoint before the old future movement schedule.
            for (int side : {-1, 1})
                if (!onlySide || onlySide == side) {
                    Endpoint point;
                    if (endpoint(0, side, point)) {
                        evaluate(point);
                        consider(point);
                    }
                }
            bool corrected = false;
            if (sweep.hintStep > 0 && (!onlySide || sweep.hintSide == onlySide) &&
                (!best.shot.target || best.time > readyTime)) {
                // A fixed number of steps travels much farther once the player
                // accelerates. Reproject the hint onto the new physical path,
                // rather than losing it when its old step overshoots 50u.
                const auto direction = left * float(sweep.hintSide);
                const auto delta = sweep.hintOrigin - start.origin;
                const float wanted = (std::max)(0.f, delta.x * direction.x + delta.y * direction.y);
                Endpoint nearest{};
                float error = FLT_MAX;
                for (int step = 0; step <= 128 && step <= window; ++step) {
                    Endpoint point;
                    if (!endpoint(step, sweep.hintSide, point))
                        break;
                    const auto travel = point.state.origin - start.origin;
                    const float distance = travel.x * direction.x + travel.y * direction.y;
                    const float difference = std::abs(distance - wanted);
                    if (difference < error) {
                        nearest = point;
                        error = difference;
                    }
                    if (distance >= wanted)
                        break;
                }
                if (error < FLT_MAX) {
                    if (nearest.step != sweep.hintStep)
                        sweep.queries[nearest.side > 0][nearest.step] =
                            sweep.queries[sweep.hintSide > 0][sweep.hintStep];
                    const auto hintCursor = sweep.queries[nearest.side > 0][nearest.step];
                    evaluate(nearest);
                    consider(nearest);
                    if (!nearest.shot.target && sweep.recover) {
                        // Moving away from an opening can round the old winning
                        // position to the tick just inside cover. Use this slice's
                        // fresh-search slot for its outward neighbor. This never
                        // carries a previous command's target into the result.
                        Endpoint neighbor;
                        if (endpoint(nearest.step + 1, sweep.hintSide, neighbor)) {
                            sweep.queries[neighbor.side > 0][neighbor.step] = hintCursor;
                            evaluate(neighbor);
                            consider(neighbor);
                            corrected = true;
                        }
                    }
                }
            }
            // An unsuccessful local correction alternates with the global sweep,
            // so a vanished opening cannot starve the other side. Both use the
            // same time slice; an incomplete global query keeps its position.
            sweep.recover = !corrected || bool(best.shot.target);
            int visited = 0;
            while (visited < 8 && std::chrono::steady_clock::now() < deadline) {
                if (onlySide)
                    sweep.side = onlySide;
                const int step = sweep.step, side = sweep.side;
                Endpoint point;
                if (endpoint(step, side, point)) {
                    const bool complete = evaluate(point, true);
                    consider(point);
                    if (!complete)
                        break;
                }
                if (onlySide || sweep.side == 1) {
                    ++sweep.step;
                    sweep.side = onlySide ? onlySide : -1;
                } else
                    sweep.side = 1;
                const bool leftEnded = paths[0].ended && step >= paths[0].count;
                const bool rightEnded = paths[1].ended && step >= paths[1].count;
                if (sweep.step > window || sweep.step > 128 ||
                    (onlySide ? (onlySide < 0 ? leftEnded : rightEnded) : (leftEnded && rightEnded)) ||
                    (best.shot.target && startTime + sweep.step > best.time)) {
                    sweep.step = 0;
                    sweep.side = onlySide ? onlySide : -1;
                    break;
                }
                ++visited;
            }
            if (best.shot.target) {
                sweep.hintStep = best.step;
                sweep.hintSide = best.side;
                sweep.hintOrigin = best.state.origin;
            }
            // Keep only the search hint after a miss. Its full selection is always
            // re-run above; clearing it here would restart acquisition on motion.
            return best;
        };
        Endpoint first{};
        if (result.first)
            first = {initial, 0, 0, 0, {result.first, firstLethal}};
        else {
            first = scan(search.first, initial, 0, cooldown, 128, 0, {}, accuracy, false, {});
            result.first = first.shot.target;
            firstLethal = first.shot.lethal;
        }
        if (!result.first || !pair || !firstLethal) {
            search.second = {};
            search.firstTarget = {};
            return result;
        }
        if (search.firstTarget != result.first.handle) {
            search.second = {};
            search.firstTarget = result.first.handle;
        }
        auto afterShot = accuracy;
        afterShot.liveAccuracy =
            first.time ? accuracy.At(first.state, first.time) : Legitbot::ReadFireAccuracy(local, weapon);
        const int mode = afterShot.liveAccuracy.mode;
        const float fire = Data(data, "CCSWeaponBaseVData->m_flInaccuracyFire", mode);
        afterShot.penalty = (first.time ? accuracy.PenaltyAt(first.state, first.time, mode) : accuracy.penalty) + fire;
        afterShot.liveAccuracy.inaccuracy += fire;
        afterShot.liveAccuracy.recoil += 1.f;
        afterShot.scopeTicks = (std::max)(0, scopeTicks - first.time);
        const int window = (std::max)(2, int(std::ceil(data->m_flCycleTimePrimary() / tick)));
        const auto second = scan(search.second,
                                 first.state,
                                 first.time,
                                 first.time + 2,
                                 window,
                                 first.step ? first.side : 0,
                                 result.first.handle,
                                 afterShot,
                                 true,
                                 {});
        result.second = second.shot.target;
        return result;
    }

    bool MovementReady(CUserCmd* cmd, C_CSPlayerPawn* local, C_CSWeaponBase* weapon, const Settings& c, bool seedShot) {
        if (c.force_no_spread)
            return true;
        if (!c.auto_stop)
            return true;
        if (!(local->m_fFlags() & FL_ONGROUND) && !c.air_stop)
            return true;
        auto* data = weapon->Data();
        if (!data)
            return false;
        const float maximum = Data(data, "CCSWeaponBaseVData->m_flMaxSpeed", std::clamp(weapon->m_weaponMode(), 0, 1));
        Vector_t displacement{}, velocity{};
        if (!CommandDisplacement(cmd, local, displacement, &velocity))
            return false;
        // Weapon processing follows movement. Use the same final velocity as
        // firing accuracy, rather than waiting for next command's pawn state.
        const float speed = std::hypot(velocity.x, velocity.y);
        const float threshold = c.stop_mode == 0 ? 1.f : maximum * .34f + .5f;
        if (speed <= threshold)
            return true;
        // Once air braking is observed, let HC test the live firing accuracy too.
        // Air pulses need not settle exactly at the ground slowwalk threshold.
        return (seedShot || (!(local->m_fFlags() & FL_ONGROUND) && c.air_stop)) && c.stop_mode == 1 && braking &&
               speed < initialSpeed - .1f;
    }
} // namespace Rage
