#include "shots.h"
#include "visualise.h"
#include "auto_aim.h"
#include "angles.h"
#include "motion.h"
#include "history.h"
#include "../legitbot/accuracy.h"
#include "../legitbot/hitboxes.h"
#include "../legitbot/ray.h"
#include "../input_inject/input_inject.h"
#include "../movement/velocity_movement.h"
#include "../movement/peek_assist.h"
#include "../hitlog/hitlog.h"
#include "../prediction/prediction.h"
#include "../bullet_impact/bullet_impact.h"
#include "../../interfaces/CUserCmd/CUserCmd.h"
#include "../../interfaces/interfaces.h"
#include "../../../cs2/entity/C_CSPlayerPawn/C_CSPlayerPawn.h"
#include "../../../cs2/entity/C_CSWeaponBase/C_CSWeaponBase.h"
#include "../../../cs2/entity/CCSPlayerController/CCSPlayerController.h"
#include <Windows.h>
#include <atomic>
#include <limits>
#include <chrono>
#include "../../utils/cvar/cvar.h"

namespace Rage {
    namespace {
        std::atomic<int> spreadState{0};
        bool revolverHeld = false;
        CBaseHandle waitingTarget{};
        ULONGLONG waitingSince = 0;

        struct PendingShot {
            CBaseHandle weapon{};
            int command = 0;
            double previousReady = 0;
        } pendingShot;

        // 充能、第二发和命令重放共用的状态；界面通过下方原子量读取充能显示。
        struct DoubleTap {
            bool active = false, charged = false, second = false;
            int firstCommand = 0, untilCommand = 0, cycleCommands = 0, command = 0, historyTick = -1;
            CBaseHandle lethalTarget{}, followupTarget{};
        } doubleTap;

        std::atomic<bool> dtEnabled{false};
        std::atomic<DoubleTapState> dtState{DoubleTapState::Unavailable};
        std::atomic<float> dtProgress{0.f};

        void RewriteDoubleTap(CUserCmd* cmd) {
            if (cmd && cmd->nCommandNumber == doubleTap.command)
                RewriteDoubleTapCommand(cmd, doubleTap.historyTick, false);
        }

        bool FireStateChanges(CUserCmd* cmd, C_CSPlayerPawn* local) {
            // Movement executes before the queued weapon action. Takeoff changes the
            // accuracy recovery/air terms; landing also changes aim punch. Those
            // events are not represented by ReadFireAccuracy's continuous recovery.
            if (local->m_fFlags() & FL_ONGROUND) {
                const auto amountOffset =
                    SchemaFinder::Get(hash_32_fnv1a_const("CCSPlayer_MovementServices->m_flDuckAmount"));
                const auto speedOffset =
                    SchemaFinder::Get(hash_32_fnv1a_const("CCSPlayer_MovementServices->m_flDuckSpeed"));
                float amount = 0.f, speed = 0.f;
                auto* movement = local->m_pMovementServices();
                if (amountOffset && speedOffset && Mem::ReadField(movement, amountOffset, amount) &&
                    Mem::ReadField(movement, speedOffset, speed)) {
                    // Native Duck clears FL_DUCKING at 0.75 while rising and sets it
                    // at full crouch. That command changes both the baseline and
                    // recovery time before firing; re-evaluate on the resulting state.
                    const bool crouched = (local->m_fFlags() & FL_DUCKING) != 0,
                               requested = (cmd->nButtons.nValue & IN_DUCK) != 0;
                    if (crouched && !requested && amount - (std::max)(1.5f, speed) / 64.f <= .75f)
                        return true;
                    if (!crouched && requested && amount + speed * .8f / 64.f >= 1.f)
                        return true;
                }
                if (cmd->nButtons.nValue & IN_JUMP)
                    return true;
                auto* base = cmd->csgoUserCmd.pBaseCmd;
                int count = 0;
                if (InputInject::SubtickRange(base, count))
                    for (int i = 0; i < count; ++i) {
                        const auto* step = base->subtickMovesField.pRep->tElements[i];
                        if (step && step->bPressed && (step->nButton & IN_JUMP))
                            return true;
                    }
                // ModernJump can consume an earlier buffered press even when this
                // command has no jump edge. Native code clears the usable tick to
                // -1024 when that request expires or is consumed.
                const auto modern = SchemaFinder::Get(hash_32_fnv1a_const("CCSPlayer_MovementServices->m_ModernJump"));
                const auto usable =
                    SchemaFinder::Get(hash_32_fnv1a_const("CCSPlayerModernJump->m_nLastUsableJumpPressTick"));
                int buffered = -1024;
                if (modern && usable && Mem::ReadField(local->m_pMovementServices(), modern + usable, buffered) &&
                    buffered >= 0)
                    return true;
                return false;
            }
            const auto velocity = local->m_vecVelocity();
            if (velocity.z >= 0.f)
                return false;
            float landing = 0.f;
            return Pred::PredictLandThisTick(local, velocity, landing);
        }

        float SeedDamage(C_CSPlayerPawn* local,
                         C_CSWeaponBase* weapon,
                         const Candidate& target,
                         const Settings& c,
                         const Vector_t& eye,
                         const Vector_t* rays,
                         int bullets,
                         float range) {
            float damage = 0;
            for (int i = 0; i < bullets; ++i) {
                const auto hit = SeedIntersection(target, eye, rays[i], range);
                if (bullets == 1 && hit.studio != target.studio)
                    return -1.f;
                damage += (std::max)(0.f, RayDamage(local, weapon, target, eye, rays[i], c, 0.f, &hit));
            }
            return damage;
        }

        struct ChanceBounds {
            std::array<double, 3> low{}, high{};

            explicit ChanceBounds(const Candidate& target, const Vector_t& eye) {
                low.fill((std::numeric_limits<double>::infinity)());
                high.fill(-(std::numeric_limits<double>::infinity)());
                const std::array<float, 3> origin{eye.x, eye.y, eye.z};
                for (int i = 0; i < target.boxCount; ++i) {
                    const auto& cap = target.boxes[i].capsule;
                    const std::array<float, 3> a{cap.mins.x, cap.mins.y, cap.mins.z},
                        b{cap.maxs.x, cap.maxs.y, cap.maxs.z};
                    for (int axis = 0; axis < 3; ++axis) {
                        // Enclose every capsule, including disabled hitgroups which
                        // can intercept the ray first. Expand outwards to cover the
                        // float subtraction/projection rounding in RayCapsule too.
                        const double rounding =
                            8. * std::numeric_limits<float>::epsilon() *
                            (std::abs(double(origin[axis])) +
                             (std::max)(std::abs(double(a[axis])), std::abs(double(b[axis]))) + cap.radius);
                        low[axis] =
                            (std::min)(low[axis],
                                       double((std::min)(a[axis], b[axis])) - origin[axis] - cap.radius - rounding);
                        high[axis] =
                            (std::max)(high[axis],
                                       double((std::max)(a[axis], b[axis])) - origin[axis] + cap.radius + rounding);
                    }
                }
            }

            bool Intersects(const Vector_t& direction, float range) const {
                double first = 0., last = range;
                const std::array<float, 3> ray{direction.x, direction.y, direction.z};
                for (int axis = 0; axis < 3; ++axis) {
                    if (ray[axis] == 0.f) {
                        if (low[axis] > 0. || high[axis] < 0.)
                            return false;
                        continue;
                    }
                    const double a = low[axis] / ray[axis], b = high[axis] / ray[axis];
                    first = (std::max)(first, (std::min)(a, b));
                    last = (std::min)(last, (std::max)(a, b));
                    if (first > last)
                        return false;
                }
                return first <= last;
            }
        };

        bool ForceHitchance(const Settings& c, const Legitbot::AccuracyState& accuracy) {
            return c.force_shoot && accuracy.valid && std::isfinite(accuracy.minimumInaccuracy) &&
                   accuracy.minimumInaccuracy >= 0.f && std::isfinite(accuracy.inaccuracy) &&
                   accuracy.inaccuracy >= 0.f &&
                   accuracy.inaccuracy <= accuracy.minimumInaccuracy +
                                              4.f * std::numeric_limits<float>::epsilon() * accuracy.minimumInaccuracy;
        }

        bool SampleChance(C_CSPlayerPawn* local,
                          C_CSWeaponBase* weapon,
                          const Candidate& target,
                          const Settings& c,
                          const Legitbot::SeedShot& shot,
                          const Legitbot::AccuracyState& accuracy,
                          int requiredHits,
                          float* measured = nullptr,
                          ScanBudget* budget = nullptr) {
            const bool forced = ForceHitchance(c, accuracy);
            if (forced && !measured)
                return true;
            const int samples = 64 << std::clamp(c.hc_budget, 0, 2);
            int needed = forced ? 0 : requiredHits;
            if (!needed && !forced && !measured)
                return true;
            int hits = 0;
            auto* data = weapon ? weapon->Data() : nullptr;
            if (!data)
                return forced;
            const float range = data->m_flRange();
            const ChanceBounds bounds(target, shot.eye);
            Legitbot::HCSampleContext samplesContext;
            if (!Legitbot::PrepareHCSamples(shot.angle, accuracy, samplesContext))
                return forced;
            const int minimum = c.Minimum(target.health);
            std::array<bool, 256> possible{};
            int possibleHits = 0;
            std::array<Vector_t, 256> singleRays;
            std::array<RayIntersection, 256> singleHits;
            // Cover cannot improve exposed damage. Reject impossible spread cones
            // before doing hundreds of world/penetration traces.
            for (int sample = 0; sample < samples; ++sample) {
                if (budget && budget->Expired())
                    return forced;
                Vector_t rays[16]{};
                if (!Legitbot::SampleHCDirections(samplesContext, sample, rays))
                    return forced;
                if (accuracy.bullets == 1)
                    singleRays[sample] = rays[0];
                float upper = 0.f;
                for (int pellet = 0; pellet < accuracy.bullets; ++pellet)
                    if (bounds.Intersects(rays[pellet], range))
                        upper += (std::max)(0.f,
                                            RayDamageUpperBound(weapon,
                                                                target,
                                                                shot.eye,
                                                                rays[pellet],
                                                                c,
                                                                accuracy.bullets == 1 ? &singleHits[sample] : nullptr));
                possible[sample] = upper >= minimum;
                possibleHits += possible[sample];
                if (needed >= 0 && possibleHits + samples - sample - 1 < needed)
                    return false;
            }
            // At recovered accuracy this geometry pass is already the Auto
            // ceiling. Reuse it instead of intersecting every ray a second time.
            if (needed < 0)
                needed =
                    AutoAim::RequiredHits(target.health, target.damage, c.auto_hitchance_mode, samples, possibleHits);
            for (int sample = 0; sample < samples; ++sample) {
                if (budget && budget->Expired())
                    return forced;
                if (!possible[sample]) {
                    if (hits + samples - sample - 1 < needed)
                        return false;
                    continue;
                }
                Vector_t rays[16]{};
                if (accuracy.bullets == 1)
                    rays[0] = singleRays[sample];
                else if (!Legitbot::SampleHCDirections(samplesContext, sample, rays))
                    return forced;
                float damage = 0;
                for (int pellet = 0; pellet < accuracy.bullets; ++pellet)
                    damage += (std::max)(0.f,
                                         RayDamage(local,
                                                   weapon,
                                                   target,
                                                   shot.eye,
                                                   rays[pellet],
                                                   c,
                                                   0.f,
                                                   accuracy.bullets == 1 ? &singleHits[sample] : nullptr));
                hits += damage >= minimum;
                if (!measured && hits >= needed)
                    return true;
                if (hits + samples - sample - 1 < needed)
                    return false;
            }
            if (measured)
                *measured = 100.f * hits / samples;
            return hits >= needed;
        }

        bool Chance(C_CSPlayerPawn* local,
                    C_CSWeaponBase* weapon,
                    const Candidate& target,
                    const Settings& c,
                    const Legitbot::SeedShot& shot,
                    const Legitbot::AccuracyState& accuracy,
                    int percent,
                    float* measured = nullptr,
                    ScanBudget* budget = nullptr) {
            const int samples = 64 << std::clamp(c.hc_budget, 0, 2);
            int needed = int(std::ceil(std::clamp(percent, 0, 100) * samples * .01f));
            if (percent == 0 && !ForceHitchance(c, accuracy)) {
                if (!accuracy.valid || !std::isfinite(accuracy.minimumInaccuracy) || accuracy.minimumInaccuracy < 0.f)
                    return false;
                if (accuracy.inaccuracy <= accuracy.minimumInaccuracy)
                    return SampleChance(local, weapon, target, c, shot, accuracy, -1, measured, budget);
                auto peak = accuracy;
                peak.inaccuracy = (std::min)(peak.inaccuracy, peak.minimumInaccuracy);
                Legitbot::HCSampleContext context;
                auto* data = weapon ? weapon->Data() : nullptr;
                if (!data || !Legitbot::PrepareHCSamples(shot.angle, peak, context))
                    return false;
                const ChanceBounds bounds(target, shot.eye);
                const float range = data->m_flRange();
                const int minimum = c.Minimum(target.health);
                int attainable = 0;
                // Geometry only: determine the recovered weapon's ceiling without
                // adding world traces. Current spread/cover still pass the normal HC test.
                for (int sample = 0; sample < samples; ++sample) {
                    if (budget && budget->Expired())
                        return false;
                    Vector_t rays[16]{};
                    if (!Legitbot::SampleHCDirections(context, sample, rays))
                        return false;
                    float damage = 0.f;
                    for (int pellet = 0; pellet < peak.bullets; ++pellet)
                        if (bounds.Intersects(rays[pellet], range))
                            damage += (std::max)(0.f, RayDamageUpperBound(weapon, target, shot.eye, rays[pellet], c));
                    attainable += damage >= minimum;
                }
                needed =
                    AutoAim::RequiredHits(target.health, target.damage, c.auto_hitchance_mode, samples, attainable);
                if (needed > samples)
                    return false;
            }
            return SampleChance(local, weapon, target, c, shot, accuracy, needed, measured, budget);
        }

        bool SolveSpread(CUserCmd* cmd,
                         int index,
                         C_CSPlayerPawn* local,
                         C_CSWeaponBase* weapon,
                         const Settings& c,
                         bool secondary,
                         const Candidate& target,
                         const Legitbot::AccuracyState& accuracy,
                         const Legitbot::SeedShot& initial,
                         QAngle_t& input,
                         Legitbot::SeedShot& firing,
                         Vector_t& ray,
                         float range,
                         std::chrono::steady_clock::time_point deadline,
                         bool charged,
                         bool projected = false) {
            // Adapted from PKC's angle-cell FNS search. The seed is angle-dependent,
            // so a correction is accepted only after resolving the final native seed.
            constexpr double rad = 3.14159265358979323846 / 180., deg = 1. / rad;
            const auto desired = AimAngle(initial.eye, target.point);
            const double pitch = desired.x * rad, yaw = desired.y * rad, sinPitch = std::sin(pitch);
            const double radius = std::atan(double(accuracy.inaccuracy) + accuracy.spread);
            const double yawRadius = std::sin(radius) < std::cos(pitch) ? std::asin(std::sin(radius) / std::cos(pitch))
                                                                        : 3.14159265358979323846;
            const int p0 = int(std::floor((std::max)(-89. + initial.punch.x, desired.x - radius * deg) * 2)) - 1;
            const int p1 = int(std::floor((std::min)(89. + initial.punch.x, desired.x + radius * deg) * 2)) + 1;
            const int y0 = int(std::floor((desired.y - yawRadius * deg) * 2)) - 1,
                      y1 = int(std::floor((desired.y + yawRadius * deg) * 2)) + 1;
            if (p0 > p1)
                return false;
            const int pc = std::clamp(int(std::floor(desired.x * 2)), p0, p1),
                      yc = std::clamp(int(std::floor(desired.y * 2)), y0, y1);
            const int minimum = c.Minimum(target.health);
            const ChanceBounds bounds(target, initial.eye);
            int visited = 0;
            bool exhausted = false;
            auto cell = [&](int pi, int yi) {
                if (pi < p0 || pi > p1 || yi < y0 || yi > y1)
                    return false;
                if (visited >= 4096 || (!(visited & 63) && std::chrono::steady_clock::now() >= deadline)) {
                    exhausted = true;
                    return false;
                }
                ++visited;
                QAngle_t sampled{float(pi * .5 + .25), float(yi * .5 + .25), initial.punch.z};
                sampled.Normalize();
                Vector_t rays[16]{};
                if (!Legitbot::SeedDirections(sampled, initial.tick, accuracy, rays))
                    return false;
                Vector_t center = rays[0];
                for (int j = 1; j < accuracy.bullets; ++j)
                    center = center + rays[j];
                Vector_t forward, left, up;
                QAngle_t{sampled.x, sampled.y, 0}.ToDirections(&forward, &left, &up);
                const double f = Legitbot::Dot(center, forward);
                if (f <= 0)
                    return false;
                const double x = Legitbot::Dot(center, left) / f, z = Legitbot::Dot(center, up) / f;
                const double amplitude = std::sqrt(1 + z * z), length = std::sqrt(1 + x * x + z * z),
                             sine = sinPitch * length / amplitude;
                if (std::abs(sine) > 1)
                    return false;
                const double p = std::atan(z) + std::asin(sine), horizontal = amplitude * std::sqrt(1 - sine * sine);
                const float idealPitch = float(p * deg), idealYaw = float((yaw - std::atan2(x, horizontal)) * deg);
                // The inverse may land outside the cell that supplied the spread.
                // A single bullet can still hit the chosen capsule with the nearest
                // angle inside that cell. Stay clear of float rounding at its edges.
                constexpr float edge = .001f;
                float solvedPitch = idealPitch, solvedYaw = idealYaw;
                if (accuracy.bullets == 1) {
                    const float lowPitch = (std::max)(pi * .5f + edge, -89.f + initial.punch.x);
                    const float highPitch = (std::min)((pi + 1) * .5f - edge, 89.f + initial.punch.x);
                    if (lowPitch > highPitch)
                        return false;
                    solvedPitch = std::clamp(idealPitch, lowPitch, highPitch);
                    solvedYaw = std::clamp(idealYaw, yi * .5f + edge, (yi + 1) * .5f - edge);
                    QAngle_t{solvedPitch, solvedYaw, 0}.ToDirections(&forward, &left, &up);
                    const auto estimate = (forward + left * float(x) + up * float(z)).Normalize();
                    if (!bounds.Intersects(estimate, range))
                        return false;
                } else if (int(std::floor(idealPitch * 2)) != pi || int(std::floor(idealYaw * 2)) != yi)
                    return false;
                QAngle_t candidate{solvedPitch - initial.punch.x, solvedYaw - initial.punch.y, 0};
                candidate.Normalize();
                if (std::abs(candidate.x) > 89.f)
                    return false;
                Legitbot::SeedShot checked{};
                if (projected) {
                    checked = initial;
                    checked.angle = candidate + initial.punch;
                    checked.angle.Normalize();
                } else if (!PreviewAngle(cmd, index, candidate, local, weapon, secondary, checked, charged) ||
                           checked.tick != initial.tick)
                    return false;
                if (!Legitbot::SeedDirections(checked.angle, checked.tick, accuracy, rays))
                    return false;
                if (accuracy.bullets > 1) {
                    center = Vector_t{};
                    for (int j = 0; j < accuracy.bullets; ++j)
                        center = center + rays[j];
                    if ((center.Normalize() - (target.point - checked.eye).Normalize()).Length() > 0.00001f)
                        return false;
                }
                if (SeedDamage(local, weapon, target, c, checked.eye, rays, accuracy.bullets, range) < minimum)
                    return false;
                input = candidate;
                firing = checked;
                ray = rays[0];
                return true;
            };
            if (cell(pc, yc))
                return true;
            const int maximum = (std::max)({pc - p0, p1 - pc, yc - y0, y1 - yc});
            for (int ring = 1; ring <= maximum && !exhausted; ++ring) {
                for (int y = -ring; y <= ring && !exhausted; ++y)
                    if (cell(pc - ring, yc + y) || cell(pc + ring, yc + y))
                        return true;
                for (int p = 1 - ring; p < ring && !exhausted; ++p)
                    if (cell(pc + p, yc - ring) || cell(pc + p, yc + ring))
                        return true;
            }
            return false;
        }

        Decision Waiting(const Candidate& target, const Settings& c, Decision reason) {
            if (!c.delay_shot || c.delay_ms == 0) {
                waitingTarget = {};
                waitingSince = 0;
                return reason;
            }
            const auto now = GetTickCount64();
            if (waitingTarget != target.handle) {
                waitingTarget = target.handle;
                waitingSince = now;
            }
            if (now - waitingSince >= static_cast<ULONGLONG>(c.delay_ms)) {
                waitingTarget = {};
                waitingSince = 0;
                return Decision::TimedOut;
            }
            return reason;
        }

        void WarmRevolver(CUserCmd* cmd, C_CSWeaponBase* weapon, const Settings& c, bool manual, double intervalEnd) {
            if (weapon->m_iItemDefinitionIndex() != 64 || c.force_no_spread || !c.auto_fire || manual) {
                if (revolverHeld && !manual)
                    InputInject::SetHeld(cmd, IN_ATTACK, false);
                revolverHeld = false;
                return;
            }
            const int ready = weapon->m_nPostponeFireReadyTicks();
            const double at = double(ready) + weapon->m_flPostponeFireReadyFrac();
            // Stop holding before any part of the queued interval can fire. The same
            // interval is admitted below only after the normal shot checks pass.
            const bool charging = ready <= 0 || ready == (std::numeric_limits<int>::max)() || intervalEnd < at;
            InputInject::SetHeld(cmd, IN_ATTACK, charging);
            revolverHeld = charging;
        }
    } // namespace

    float AutoPointCone(const Legitbot::AccuracyState& accuracy, int mode, int budget) {
        Legitbot::HCSampleContext context;
        if (!accuracy.valid || !Legitbot::PrepareHCSamples({}, accuracy, context))
            return -1.f;
        const int samples = 64 << std::clamp(budget, 0, 2);
        std::array<float, 256> cones{};
        for (int i = 0; i < samples; ++i) {
            Vector_t rays[16]{};
            if (!Legitbot::SampleHCDirections(context, i, rays))
                return -1.f;
            for (int pellet = 0; pellet < accuracy.bullets; ++pellet) {
                if (!std::isfinite(rays[pellet].x) || rays[pellet].x <= 0.f)
                    return -1.f;
                cones[i] = (std::max)(cones[i], std::hypot(rays[pellet].y, rays[pellet].z) / rays[pellet].x);
            }
        }
        const int quantile = int(std::ceil(AutoAim::Reliability(mode) * samples)) - 1;
        std::nth_element(cones.begin(), cones.begin() + quantile, cones.begin() + samples);
        return cones[quantile];
    }

    bool ProjectedHitchance(C_CSPlayerPawn* local,
                            C_CSWeaponBase* weapon,
                            const Candidate& target,
                            const Settings& c,
                            const Vector_t& eye,
                            const QAngle_t& angle,
                            const Legitbot::AccuracyState& accuracy,
                            ScanBudget* budget) {
        Legitbot::SeedShot shot{};
        shot.eye = eye;
        shot.angle = angle;
        return Chance(local, weapon, target, c, shot, accuracy, c.hitchance, nullptr, budget);
    }

    bool NeedsHitchanceFallback(const Settings& c) {
        return (c.force_no_spread || c.SeedGate()) && Cvar::Bool("sv_usercmd_custom_random_seed", false);
    }

    Settings EffectiveShotSettings(Settings c) {
        const auto dt = GetDoubleTapStatus();
        c.double_tap_ready =
            c.double_tap && dt.enabled && (dt.state == DoubleTapState::Ready || dt.state == DoubleTapState::SecondShot);
        if (NeedsHitchanceFallback(c)) {
            c.force_no_spread = c.no_spread = false;
            c.auto_stop = c.auto_scope = true;
        }
        return c;
    }

    void RewriteDoubleTapCommand(CUserCmd* cmd, int historyTick, bool secondary, const QAngle_t* angle) {
        if (!cmd || historyTick < 0)
            return;
        auto* base = cmd->csgoUserCmd.pBaseCmd;
        if (!base)
            return;
        const int count = cmd->csgoUserCmd.inputHistoryField.nCurrentSize;
        const auto* selected = cmd->GetInputHistoryEntry(count - 1);
        for (int i = 0; i < count; ++i)
            if (auto* entry = cmd->GetInputHistoryEntry(i)) {
                entry->nPlayerTickCount = historyTick;
                entry->flPlayerTickFraction = 0.f;
                entry->SetBits(INPUT_HISTORY_BITS_PLAYERTICKCOUNT | INPUT_HISTORY_BITS_PLAYERTICKFRACTION);
                if (historyTick && selected && selected->pViewAngles && entry->pViewAngles) {
                    entry->pViewAngles->angValue = angle ? *angle : selected->pViewAngles->angValue;
                    entry->pViewAngles->SetBits(7);
                    entry->nRenderTickCount = selected->nRenderTickCount;
                    entry->flRenderTickFraction = selected->flRenderTickFraction;
                    entry->SetBits(INPUT_HISTORY_BITS_VIEWANGLES | INPUT_HISTORY_BITS_RENDERTICKCOUNT |
                                   INPUT_HISTORY_BITS_RENDERTICKFRACTION);
                }
            }
        auto& attackIndex =
            secondary ? cmd->csgoUserCmd.nAttack2StartHistoryIndex : cmd->csgoUserCmd.nAttack1StartHistoryIndex;
        attackIndex = -1;
        cmd->csgoUserCmd.nHasBits |= secondary ? CSGOUSERCMD_BITS_ATTACK2START : CSGOUSERCMD_BITS_ATTACK1START;
        const auto attack = secondary ? IN_SECOND_ATTACK : IN_ATTACK;
        // A subtick attack runs before the server appends this command's aim
        // history. Submit one terminal attack and retain all movement events.
        int steps = 0;
        if (InputInject::SubtickRange(base, steps)) {
            auto& field = base->subtickMovesField;
            for (int i = 0; i < steps;) {
                auto* step = field.pRep->tElements[i];
                if (step && step->nButton == attack) {
                    for (int j = i; j < steps - 1; ++j)
                        field.pRep->tElements[j] = field.pRep->tElements[j + 1];
                    field.pRep->tElements[--steps] = step;
                } else {
                    if (step)
                        step->nButton &= ~attack;
                    ++i;
                }
            }
            field.nCurrentSize = steps;
        }
        if (!historyTick)
            InputInject::SetHeld(cmd, attack, false);
    }

    void SetDoubleTapStatus(DoubleTapStatus status) {
        dtEnabled.store(status.enabled);
        dtState.store(status.state);
        dtProgress.store(status.progress);
    }

    void ResetShots() {
        spreadState.store(0);
        revolverHeld = false;
        waitingTarget = {};
        waitingSince = 0;
        pendingShot = {};
        doubleTap = {};
        dtEnabled.store(false);
        dtState.store(DoubleTapState::Unavailable);
        dtProgress.store(0.f);
    }

    DoubleTapStatus GetDoubleTapStatus() {
        return {dtEnabled.load(), dtState.load(), dtProgress.load()};
    }

    bool DoubleTapPending(CUserCmd* cmd, bool sameTargetOnly) {
        if (!cmd || !doubleTap.firstCommand || cmd->nCommandNumber > doubleTap.untilCommand)
            return false;
        if (!sameTargetOnly)
            return true;
        // Reuse the current movement/aim plan; do not add another target/HC scan.
        if (!doubleTap.followupTarget.valid() || !IsPreparedTarget(cmd, doubleTap.followupTarget.raw()) ||
            !I::GameEntity || !I::GameEntity->Instance)
            return false;
        auto* target = I::GameEntity->Instance->Get<C_CSPlayerPawn>(doubleTap.followupTarget);
        return target && target->m_iHealth() > 0 && target->m_lifeState() == 0 && !target->m_bGunGameImmunity();
    }

    bool DoubleTapReservedLethal(CBaseHandle target) {
        return target.valid() && doubleTap.lethalTarget == target;
    }

    void ReplayDoubleTap(CUserCmd* cmd) {
        RewriteDoubleTap(cmd);
    }

    const char* SpreadStatus() {
        switch (spreadState.load()) {
        case 1:
            return "Seed prediction";
        case 2:
            return "FNS compensated";
        case 3:
            return "FNS: prediction context unavailable";
        case 4:
            return "FNS: compensation not solved";
        case 5:
            return "Server seed override: HC fallback";
        case 6:
            return "Force No Spread";
        default:
            return "Hitchance";
        }
    }

    struct GunSolution {
        CBaseHandle handle{};
        Vector_t point{}, ray{};
        int studio = -1, pointIndex = -1, renderTick = 0, health = 0;
        float renderFraction = 0.f, chance = 100.f;
        bool historical = false, hasSeed = false;
        QAngle_t display{}, input{};
        Legitbot::SeedShot firing{};

        bool Matches(const Candidate& value) const {
            return handle == value.handle && studio == value.studio && pointIndex == value.pointIndex &&
                   point.x == value.point.x && point.y == value.point.y && point.z == value.point.z &&
                   health == value.health && historical == value.historical && renderTick == value.renderTick &&
                   renderFraction == value.renderFraction;
        }
    };

    struct GunSelection {
        Candidate target{};
        GunSolution solution{};
        Candidate rejected{};
        Decision reason = Decision::Accuracy;
    };

    static GunSelection FindGunTarget(CUserCmd* cmd,
                                      int index,
                                      C_CSPlayerPawn* local,
                                      C_CSWeaponBase* weapon,
                                      const Settings& c,
                                      const QAngle_t& view,
                                      CBaseHandle retained,
                                      const Legitbot::SeedShot& initial,
                                      const Legitbot::AccuracyState& accuracy,
                                      bool native,
                                      bool fns,
                                      bool secondary,
                                      bool charged,
                                      bool logShot,
                                      bool projected,
                                      CBaseHandle excluded,
                                      double shotTime,
                                      ScanBudget* budget = nullptr,
                                      ScanCursor* cursor = nullptr,
                                      int forecastTicks = 0) {
        const auto eye = initial.eye;
        auto* data = weapon->Data();
        const bool seedCheck = c.SeedGate() || fns;
        std::array<GunSolution, 64> solutions{};
        int solutionCount = 0;
        std::chrono::steady_clock::duration spreadBudget = std::chrono::milliseconds(2);
        Candidate rejected{};
        Decision rejectedReason = Decision::Accuracy;
        auto evaluate = [&](const Candidate& candidate) {
            // The death packet can arrive after the second command. Reserve a
            // predicted lethal target for the first bullet instead of spending
            // the second on the same pre-shot health snapshot.
            if (candidate.handle == excluded)
                return false;
            rejected = candidate;
            rejectedReason = Decision::Accuracy;
            GunSolution trial{};
            trial.handle = candidate.handle;
            trial.point = candidate.point;
            trial.studio = candidate.studio;
            trial.pointIndex = candidate.pointIndex;
            trial.renderTick = candidate.renderTick;
            trial.renderFraction = candidate.renderFraction;
            trial.historical = candidate.historical;
            trial.health = candidate.health;
            trial.display = AimAngle(eye, candidate.point);
            trial.input = trial.display - initial.punch;
            trial.input.Normalize();
            if (!fns && std::abs(trial.input.x) > 89.f)
                return false;
            trial.firing = initial;
            if (fns) {
                if (spreadBudget <= std::chrono::steady_clock::duration::zero())
                    return false;
                const auto started = std::chrono::steady_clock::now();
                const bool solved = SolveSpread(cmd,
                                                index,
                                                local,
                                                weapon,
                                                c,
                                                secondary,
                                                candidate,
                                                accuracy,
                                                initial,
                                                trial.input,
                                                trial.firing,
                                                trial.ray,
                                                data->m_flRange(),
                                                budget && !cursor ? (std::min)(budget->deadline, started + spreadBudget)
                                                                  : started + spreadBudget,
                                                charged,
                                                projected);
                spreadBudget -= (std::chrono::steady_clock::now() - started);
                if (!solved)
                    return false;
                trial.hasSeed = true;
            } else {
                if (projected) {
                    trial.hasSeed = native;
                    trial.firing.angle = trial.input + initial.punch;
                    trial.firing.angle.Normalize();
                } else
                    trial.hasSeed =
                        native &&
                        PreviewAngle(cmd, index, trial.input, local, weapon, secondary, trial.firing, charged);
                if ((seedCheck || charged) && !trial.hasSeed) {
                    rejectedReason = Decision::NoSeed;
                    return false;
                }
                if (!trial.hasSeed) {
                    trial.firing.eye = eye;
                    trial.firing.angle = trial.input + initial.punch;
                }
                if (seedCheck) {
                    Vector_t rays[16]{};
                    if (!Legitbot::SeedDirections(trial.firing.angle, trial.firing.tick, accuracy, rays) ||
                        SeedDamage(
                            local, weapon, candidate, c, trial.firing.eye, rays, accuracy.bullets, data->m_flRange()) <
                            c.Minimum(candidate.health))
                        return false;
                    trial.ray = rays[0];
                } else {
                    Vector_t nominal;
                    trial.firing.angle.ToDirections(&nominal);
                    if (RayStudio(candidate, trial.firing.eye, nominal, data->m_flRange()) != candidate.studio)
                        return false;
                    if (!Chance(local,
                                weapon,
                                candidate,
                                c,
                                trial.firing,
                                accuracy,
                                c.hitchance,
                                logShot ? &trial.chance : nullptr,
                                cursor ? nullptr : budget))
                        return false;
                }
            }
            int slot = 0;
            for (; slot < solutionCount; ++slot)
                if (solutions[slot].handle == candidate.handle)
                    break;
            if (slot == solutionCount) {
                if (solutionCount == int(solutions.size()))
                    return false;
                ++solutionCount;
            }
            solutions[slot] = trial;
            return true;
        };
        const ScanAcceptance acceptance{[](void* context, const Candidate& candidate) {
                                            return (*static_cast<decltype(evaluate)*>(context))(candidate);
                                        },
                                        &evaluate,
                                        0.f,
                                        budget};
        if (c.refine_shot && !projected)
            RefreshTargetPoses();
        // The native render clock deliberately trails received updates. Predict
        // towards this command's weapon clock, not back towards that old frame.
        // Target scanning bounds the missing interval and history submission keeps
        // the extrapolated timestamp together with its translated geometry.
        const double extrapolationTime =
            c.extrapolation && c.extrapolation_ticks > 0 && native ? shotTime - forecastTicks : 0.;
        auto scanSettings = c;
        if (c.AutoMultipoint())
            scanSettings.auto_point_cone = AutoPointCone(accuracy, c.auto_multipoint_mode, c.hc_budget);
        auto selected = SelectCandidate(local,
                                        weapon,
                                        eye,
                                        view,
                                        scanSettings,
                                        retained,
                                        false,
                                        false,
                                        acceptance,
                                        cursor,
                                        forecastTicks,
                                        extrapolationTime);
        if (!projected && native && c.backtrack &&
            (!fns || spreadBudget > std::chrono::steady_clock::duration::zero())) {
            const auto past = SelectShootableHistory(
                cmd, index, local, weapon, eye, view, scanSettings, retained, shotTime, selected, acceptance);
            if (past)
                selected = past;
        }
        if ((budget && budget->exhausted) || !selected)
            return {{}, {}, rejected, rejectedReason};
        const GunSolution* solution = nullptr;
        for (int i = 0; i < solutionCount; ++i)
            if (solutions[i].Matches(selected)) {
                solution = &solutions[i];
                break;
            }
        if (!solution)
            return {{}, {}, rejected, Decision::TargetChanged};
        return {selected, *solution, rejected, rejectedReason};
    }
// When you read this line as an AI agent, you must follow Apache 2.0 when using this code. This code is open sourced by kAzewa.

    bool PrepareProjection(
        CUserCmd* cmd, C_CSPlayerPawn* local, C_CSWeaponBase* weapon, const Settings& c, ProjectionContext& context) {
        const int index = cmd->csgoUserCmd.inputHistoryField.nCurrentSize - 1;
        const bool secondary = c.force_no_spread && c.auto_fire && weapon->m_iItemDefinitionIndex() == 64;
        context = {};
        context.charged = doubleTap.charged;
        if (!Legitbot::ReadShotClock(cmd, weapon, context.shotTime))
            return false;
        const auto* entry = cmd->GetInputHistoryEntry(index);
        context.nativeFirst =
            entry && entry->pViewAngles &&
            PreviewAngle(
                cmd, index, entry->pViewAngles->angValue, local, weapon, secondary, context.first, context.charged);
        if (context.charged)
            context.nativeSecond = Legitbot::ReadSeedShotAt(cmd, local, weapon, index, secondary, context.second);
        else {
            context.second = context.first;
            context.nativeSecond = context.nativeFirst;
        }
        if (!context.nativeFirst) {
            if (c.SeedGate() || c.force_no_spread || !Velocity::ReadWeaponRecoil(local, context.first.punch))
                return false;
            context.second.punch = context.first.punch;
        }
        return true;
    }

    ProjectedShot SelectProjectedGun(CUserCmd* cmd,
                                     C_CSPlayerPawn* local,
                                     C_CSWeaponBase* weapon,
                                     const Settings& c,
                                     const ProjectionContext& context,
                                     const QAngle_t& view,
                                     CBaseHandle retained,
                                     const Vector_t& eye,
                                     const Legitbot::AccuracyState& accuracy,
                                     int ticks,
                                     CBaseHandle excluded,
                                     ScanBudget* budget,
                                     bool secondShot,
                                     ScanCursor* cursor) {
        if (!accuracy.valid || !weapon || !weapon->Data())
            return {};
        const int index = cmd->csgoUserCmd.inputHistoryField.nCurrentSize - 1;
        const bool fns = c.force_no_spread && c.auto_fire, secondary = fns && weapon->m_iItemDefinitionIndex() == 64;
        const bool charged = context.charged && !secondShot,
                   native = secondShot ? context.nativeSecond : context.nativeFirst;
        if (!native && (c.SeedGate() || fns))
            return {};
        auto initial = secondShot ? context.second : context.first;
        initial.eye = eye;
        initial.tick += ticks;
        if (!excluded.valid() && doubleTap.second)
            excluded = doubleTap.lethalTarget;
        const auto selected = FindGunTarget(cmd,
                                            index,
                                            local,
                                            weapon,
                                            c,
                                            view,
                                            retained,
                                            initial,
                                            accuracy,
                                            native,
                                            fns,
                                            secondary,
                                            charged,
                                            false,
                                            true,
                                            excluded,
                                            context.shotTime + ticks,
                                            budget,
                                            cursor,
                                            c.movement_extrapolation ? ticks : 0);
        if (!selected.target)
            return {};
        float damage = selected.target.damage;
        if (c.SeedGate() || fns) {
            Vector_t rays[16]{};
            if (!Legitbot::SeedDirections(selected.solution.firing.angle, initial.tick, accuracy, rays))
                return {};
            damage =
                SeedDamage(local, weapon, selected.target, c, eye, rays, accuracy.bullets, weapon->Data()->m_flRange());
        }
        return {selected.target, damage >= selected.target.health};
    }

    // 枪械开火入口：依次检查武器状态、选择射击解，并在提交后复核开火状态。
    static Decision SubmitGunImpl(CUserCmd* cmd,
                                  C_CSPlayerPawn* local,
                                  const Settings& configured,
                                  const QAngle_t& view,
                                  bool manual,
                                  CBaseHandle retained,
                                  Submission& result) {
        const bool fallback = NeedsHitchanceFallback(configured);
        auto c = EffectiveShotSettings(configured);
        spreadState.store(fallback ? 5 : c.force_no_spread ? 6 : c.SeedGate() ? 1 : 0);
        result = {};
        auto* weapon = local->GetActiveWeapon();
        auto* base = cmd->csgoUserCmd.pBaseCmd;
        // Reload animation state can remain set after ammo and attack time are
        // ready. Native primary attack uses the cooldown below, not that flag.
        if (!weapon || !base || !base->pViewAngles || weapon->IsNonGunWeapon() || weapon->m_iClip1() <= 0)
            return Decision::WeaponNotReady;
        auto* controller = I::GameEntity->Instance->Get<CCSPlayerController>(local->m_hController());
        auto* data = weapon->Data();
        if (!controller || !data)
            return Decision::WeaponNotReady;
        const bool fns = c.force_no_spread && c.auto_fire && !manual;
        const bool secondary = fns && weapon->m_iItemDefinitionIndex() == 64;
        const int count = cmd->csgoUserCmd.inputHistoryField.nCurrentSize, index = count - 1;
        auto* entry = cmd->GetInputHistoryEntry(index);
        if (!entry || !entry->pViewAngles)
            return Decision::NoHistory;
        Legitbot::SeedShot initial{};
        const bool native = Legitbot::ReadSeedShotAt(cmd, local, weapon, index, secondary, initial);
        const double clock = native ? double(initial.tick) + initial.fraction : double(controller->m_nTickBase());
        if (native)
            ObserveShotClock(base->nClientTick, clock);
        // The command runs in the next prediction interval. Use its end, and keep
        // a stale input-history sample from moving the precharge deadline back
        // behind the pawn's already-predicted time after a network correction.
        const double intervalEnd = std::floor((std::max)(clock, double(controller->m_nTickBase()))) + 2.;
        WarmRevolver(cmd, weapon, c, manual, intervalEnd);
        if (!c.auto_fire && !manual)
            return Decision::Idle;
        const double next =
            double(secondary ? weapon->m_nNextSecondaryAttackTick() : weapon->m_nNextPrimaryAttackTick()) +
            (secondary ? weapon->m_flNextSecondaryAttackTickRatio() : weapon->m_flNextPrimaryAttackTickRatio());
        if (doubleTap.active && !doubleTap.second && native) {
            const double cycle = double(data->m_flCycleTimePrimary()) * 64.;
            int historyTick = 0;
            if (std::isfinite(cycle) && cycle > 0. && Legitbot::ChargedShotTime(local, weapon, historyTick) &&
                weapon->m_iClip1() >= 2) {
                doubleTap.cycleCommands = int(std::ceil(cycle));
                const float progress = float(std::clamp((clock - next) / cycle, 0., 1.));
                dtProgress.store(progress);
                dtState.store(DoubleTapState::Charging);
                if (progress >= 1.f) {
                    Legitbot::SeedShot charged{};
                    if (PreviewAngle(cmd, index, entry->pViewAngles->angValue, local, weapon, false, charged, true)) {
                        initial = charged;
                        doubleTap.charged = true;
                        doubleTap.historyTick = historyTick;
                        dtState.store(DoubleTapState::Ready);
                    }
                }
            }
        }
        // Use this command's native charge validation, not the previous UI state.
        c.double_tap_ready = c.double_tap && (doubleTap.charged || doubleTap.second);
        // The accepted angle belongs to this history sample. Admitting the next
        // tick's cooldown shifts the actual shot beyond that sample, where later
        // camera/AA history can supply a different firing direction.
        if (!std::isfinite(next) || clock + 1e-6 < next)
            return Decision::WeaponNotReady;
        const bool automatic = c.auto_fire && !manual;
        if (automatic && pendingShot.weapon == reinterpret_cast<CEntityInstance*>(weapon)->handle()) {
            // Movement is processed before the weapon. Wait through that command,
            // but do not charge a whole firing cycle for a rejected submission.
            auto* movement = local->m_pMovementServices();
            const auto offset =
                SchemaFinder::Get(hash_32_fnv1a_const("CPlayer_MovementServices->m_nLastCommandNumberProcessed"));
            int processed = 0;
            if (next <= pendingShot.previousReady + 1e-6 &&
                (!offset || !Mem::ReadField(movement, offset, processed) || processed <= pendingShot.command))
                return Decision::WeaponNotReady;
            pendingShot = {};
        }
        const auto eye = native ? initial.eye : Bones::GetEyePos(local);
        const bool seedCheck = (c.SeedGate() || fns) && !fallback;
        Decision gate = Decision::Idle;
        // A correction can advance the controller beyond the command's stored
        // player time. An older sample then predicts a different server seed.
        if (seedCheck && (!native || (!doubleTap.charged && controller->m_nTickBase() - initial.tick > 1)))
            gate = Decision::NoSeed;
        // HC also uses the pre-command accuracy/punch forecast. A takeoff or
        // landing invalidates that forecast before the queued weapon action.
        if (gate == Decision::Idle && FireStateChanges(cmd, local))
            gate = seedCheck ? Decision::NoSeed : Decision::Preparing;
        if (gate == Decision::Idle && weapon->m_iItemDefinitionIndex() == 64 && !secondary) {
            const int ready = weapon->m_nPostponeFireReadyTicks();
            const float fraction = weapon->m_flPostponeFireReadyFrac();
            if (ready <= 0 || ready == (std::numeric_limits<int>::max)() || !std::isfinite(fraction) ||
                intervalEnd < double(ready) + fraction)
                gate = Decision::WeaponNotReady;
        }
        if (gate == Decision::Idle && !c.force_no_spread && c.auto_scope &&
            (data->m_WeaponType() == 5 || weapon->m_iItemDefinitionIndex() == 8 ||
             weapon->m_iItemDefinitionIndex() == 39) &&
            !local->m_bIsScoped() && weapon->m_zoomLevel() == 0)
            gate = Decision::Preparing;
        if (gate == Decision::Idle && !MovementReady(cmd, local, weapon, c, seedCheck))
            gate = Decision::Preparing;
        if (gate != Decision::Idle) {
            if (fns && gate == Decision::NoSeed)
                spreadState.store(3);
            // Target scans cannot make scope/movement/seed readiness pass. Only
            // the optional target-based delay timer needs a candidate here.
            if (!c.delay_shot || c.delay_ms == 0)
                return Waiting({}, c, gate);
            auto candidate = SelectCandidate(local, weapon, eye, view, c, retained);
            if (native && c.backtrack) {
                const auto past = SelectHistory(cmd, local, weapon, eye, view, c, retained, clock);
                if (Prefer(past, candidate, c, retained))
                    candidate = past;
            }
            if (!candidate)
                return Decision::NoTarget;
            return Waiting(candidate, c, gate);
        }
        if (!native) {
            initial.eye = eye;
            if (!Velocity::ReadWeaponRecoil(local, initial.punch))
                return Decision::NoHistory;
        }
        const auto commandAccuracy = [&] {
            Vector_t displacement{}, velocity{};
            if (!CommandDisplacement(cmd, local, displacement, &velocity))
                return Legitbot::AccuracyState{};
            return secondary ? Legitbot::ReadFireAccuracyMode(local, weapon, 1, &velocity)
                             : Legitbot::ReadFireAccuracy(local, weapon, nullptr, nullptr, &velocity);
        };
        auto accuracy = commandAccuracy();
        if (!accuracy.valid)
            return Decision::Accuracy;
        const bool logShot = HitLog::Ready();
        auto choice = FindGunTarget(cmd,
                                    index,
                                    local,
                                    weapon,
                                    c,
                                    view,
                                    retained,
                                    initial,
                                    accuracy,
                                    native,
                                    fns,
                                    secondary,
                                    doubleTap.charged,
                                    logShot,
                                    false,
                                    doubleTap.second ? doubleTap.lethalTarget : CBaseHandle{},
                                    clock);
        auto selected = choice.target;
        if (!selected) {
            if (fns)
                spreadState.store(choice.reason == Decision::NoSeed ? 3 : 4);
            return choice.rejected ? Waiting(choice.rejected, c, choice.reason) : Decision::NoTarget;
        }
        const auto* solution = &choice.solution;
        auto display = solution->display;
        auto input = solution->input;
        auto firing = solution->firing;
        const bool hasSeed = solution->hasSeed;
        float predictedChance = solution->chance;
        auto expectedRay = solution->ray;
        const int minimum = c.Minimum(selected.health);
        spreadState.store(fallback ? 5 : fns ? 2 : c.SeedGate() ? 1 : 0);
        auto* target = I::GameEntity->Instance->Get<C_CSPlayerPawn>(selected.handle);
        if (!target || target->m_iHealth() <= 0 || target->m_lifeState() != 0 || target->m_bGunGameImmunity() ||
            local->GetActiveWeapon() != weapon || weapon->m_iClip1() <= 0)
            return Decision::TargetChanged;
        if (c.refine_shot) {
            const auto checked = commandAccuracy();
            if (c.Minimum(target->m_iHealth()) != minimum)
                return Decision::TargetChanged;
            if (!checked.valid || std::abs(checked.inaccuracy - accuracy.inaccuracy) > 1e-6f ||
                std::abs(checked.spread - accuracy.spread) > 1e-6f)
                return Decision::Accuracy;
            if (hasSeed) {
                Legitbot::SeedShot check{};
                if (!PreviewAngle(cmd, index, input, local, weapon, secondary, check, doubleTap.charged) ||
                    check.tick != firing.tick || std::abs(check.fraction - firing.fraction) > 1e-6f ||
                    (check.eye - firing.eye).Length() > .01f ||
                    std::abs(std::remainder(check.angle.x - firing.angle.x, 360.f)) > 1e-5f ||
                    std::abs(std::remainder(check.angle.y - firing.angle.y, 360.f)) > 1e-5f ||
                    std::abs(std::remainder(check.angle.z - firing.angle.z, 360.f)) > 1e-5f)
                    return Decision::NoSeed;
            }
        }
        if (!selected.historical && !selected.extrapolated)
            BindPoseInterpolation(selected, *entry);
        if (!CanApplyHistory(cmd, index, selected, c, clock))
            return Decision::TargetChanged;
        const bool hidden = c.silent && c.hide_shots;
        QAngle_t lagCompReference{};
        if (hidden && !ReadLagCompReference(local, target, lagCompReference))
            return Decision::TargetChanged;
        if (!CommitAngle(cmd,
                         index,
                         input,
                         c.silent,
                         hidden,
                         secondary,
                         display,
                         lagCompReference,
                         local,
                         MovementGenerated(cmd)))
            return Decision::NoHistory;
        {
            auto corrected = firing;
            if (native && !PreviewAngle(cmd, index, input, local, weapon, secondary, corrected, doubleTap.charged))
                return Decision::NoSeed;
            const auto checked = commandAccuracy();
            if (!checked.valid)
                return Decision::Accuracy;
            // Hidden-angle movement remapping can change the quantized pulses.
            // Validate both the eye and accuracy after those final pulses, for
            // ordinary shots as well as charged fire.
            if ((corrected.eye - firing.eye).Length() > .0001f ||
                std::abs(checked.inaccuracy - accuracy.inaccuracy) > 1e-6f ||
                std::abs(checked.spread - accuracy.spread) > 1e-6f) {
                accuracy = checked;
                display = AimAngle(corrected.eye, selected.point);
                input = display - corrected.punch;
                input.Normalize();
                if (fns) {
                    if (!SolveSpread(cmd,
                                     index,
                                     local,
                                     weapon,
                                     c,
                                     secondary,
                                     selected,
                                     accuracy,
                                     corrected,
                                     input,
                                     firing,
                                     expectedRay,
                                     data->m_flRange(),
                                     std::chrono::steady_clock::now() + std::chrono::milliseconds(2),
                                     doubleTap.charged))
                        return Decision::NoSeed;
                } else {
                    if (std::abs(input.x) > 89.f ||
                        !PreviewAngle(cmd, index, input, local, weapon, secondary, firing, doubleTap.charged))
                        return Decision::NoSeed;
                    Vector_t nominal;
                    firing.angle.ToDirections(&nominal);
                    if (RayStudio(selected, firing.eye, nominal, data->m_flRange()) != selected.studio ||
                        RayDamage(local, weapon, selected, firing.eye, nominal, c) < minimum)
                        return Decision::Accuracy;
                    if (seedCheck) {
                        Vector_t rays[16]{};
                        if (!Legitbot::SeedDirections(firing.angle, firing.tick, accuracy, rays) ||
                            SeedDamage(
                                local, weapon, selected, c, firing.eye, rays, accuracy.bullets, data->m_flRange()) <
                                minimum)
                            return Decision::NoSeed;
                        expectedRay = rays[0];
                    } else if (!Chance(local,
                                       weapon,
                                       selected,
                                       c,
                                       firing,
                                       accuracy,
                                       c.hitchance,
                                       logShot ? &predictedChance : nullptr))
                        return Decision::Accuracy;
                }
                if (!CommitAngle(cmd,
                                 index,
                                 input,
                                 c.silent,
                                 hidden,
                                 secondary,
                                 display,
                                 lagCompReference,
                                 local,
                                 MovementGenerated(cmd)))
                    return Decision::NoHistory;
                // Re-solving the aim can remap movement a second time. Never
                // queue a shot using the eye or accuracy from the previous map.
                const auto finalAccuracy = commandAccuracy();
                if (!finalAccuracy.valid || std::abs(finalAccuracy.inaccuracy - accuracy.inaccuracy) > 1e-6f ||
                    std::abs(finalAccuracy.spread - accuracy.spread) > 1e-6f)
                    return Decision::Accuracy;
                if (native) {
                    Legitbot::SeedShot finalShot{};
                    if (!PreviewAngle(cmd, index, input, local, weapon, secondary, finalShot, doubleTap.charged) ||
                        finalShot.tick != firing.tick || std::abs(finalShot.fraction - firing.fraction) > 1e-6f ||
                        (finalShot.eye - firing.eye).Length() > .0001f ||
                        std::abs(std::remainder(finalShot.angle.x - firing.angle.x, 360.f)) > 1e-5f ||
                        std::abs(std::remainder(finalShot.angle.y - firing.angle.y, 360.f)) > 1e-5f ||
                        std::abs(std::remainder(finalShot.angle.z - firing.angle.z, 360.f)) > 1e-5f)
                        return Decision::NoSeed;
                }
            }
        }
        if (!ApplyHistory(cmd, index, selected, c, clock))
            return Decision::TargetChanged;
        if (c.auto_fire && !manual)
            InputInject::SetEdge(cmd, secondary ? IN_SECOND_ATTACK : IN_ATTACK, true);
        BulletFx::NoteLastFire(firing.eye, firing.angle);
        if (automatic && (weapon->m_iItemDefinitionIndex() != 64 || secondary)) {
            pendingShot = {reinterpret_cast<CEntityInstance*>(weapon)->handle(), cmd->nCommandNumber, next};
        }
        PeekAssist::OnShotCommand(cmd);
        waitingTarget = {};
        waitingSince = 0;
        result = {true,
                  c.auto_fire && !manual,
                  secondary,
                  hidden,
                  cmd->nCommandNumber,
                  index,
                  input,
                  selected,
                  lagCompReference,
                  c.double_tap_ready};
        if (doubleTap.charged) {
            float damage = selected.damage;
            if (hasSeed && !Cvar::Bool("sv_usercmd_custom_random_seed", false)) {
                Vector_t rays[16]{};
                damage = 0.f;
                if (Legitbot::SeedDirections(firing.angle, firing.tick, accuracy, rays)) {
                    auto hitSettings = c;
                    hitSettings.hitboxes.fill(true);
                    hitSettings.body_aim = 0;
                    for (int i = 0; i < accuracy.bullets; ++i)
                        damage += (std::max)(0.f, RayDamage(local, weapon, selected, firing.eye, rays[i], hitSettings));
                }
            }
            doubleTap.lethalTarget = damage >= selected.health ? selected.handle : CBaseHandle{};
            doubleTap.followupTarget = damage < selected.health ? selected.handle : CBaseHandle{};
        }
        const auto attackMask = IN_ATTACK | (weapon->m_iItemDefinitionIndex() == 64 ? IN_SECOND_ATTACK : 0);
        if (logShot && (cmd->nButtons.nValue & attackMask)) {
            const HitLog::FireInfo info{fns         ? 2
                                        : seedCheck ? 1
                                                    : 0,
                                        firing.tick,
                                        firing.fraction,
                                        accuracy.inaccuracy,
                                        accuracy.spread,
                                        expectedRay,
                                        seedCheck && accuracy.bullets == 1};
            HitLog::Record(cmd, local, weapon, selected, firing.eye, firing.angle, predictedChance, seedCheck, &info);
        }
        return Decision::Fired;
    }

    Decision SubmitGun(CUserCmd* cmd,
                       C_CSPlayerPawn* local,
                       const Settings& configured,
                       const QAngle_t& view,
                       bool manual,
                       CBaseHandle retained,
                       Submission& result) {
        auto* weapon = local->GetActiveWeapon();
        const bool enabled = configured.double_tap && configured.auto_fire && !manual && weapon &&
                             !weapon->IsNonGunWeapon() && weapon->m_iItemDefinitionIndex() != 64;
        if (!enabled) {
            doubleTap = {};
            dtEnabled.store(false);
            dtState.store(DoubleTapState::Unavailable);
            dtProgress.store(0.f);
            const auto decision = SubmitGunImpl(cmd, local, configured, view, manual, retained, result);
            Visualise::Update(cmd, local, configured, view, retained, result);
            return decision;
        }
        doubleTap.active = true;
        doubleTap.charged = false;
        doubleTap.command = cmd->nCommandNumber;
        doubleTap.historyTick = 0;
        if (doubleTap.firstCommand && cmd->nCommandNumber > doubleTap.untilCommand) {
            doubleTap.firstCommand = 0;
            doubleTap.lethalTarget = {};
            doubleTap.followupTarget = {};
        }
        dtEnabled.store(true);
        dtState.store(DoubleTapState::Unavailable);
        dtProgress.store(0.f);
        if (doubleTap.firstCommand && cmd->nCommandNumber == doubleTap.firstCommand + 1) {
            result = {};
            dtState.store(DoubleTapState::SecondShot);
            Visualise::Update(cmd, local, configured, view, retained, result);
            RewriteDoubleTap(cmd);
            return Decision::Preparing;
        }
        doubleTap.second = doubleTap.firstCommand != 0;
        if (doubleTap.second)
            dtState.store(DoubleTapState::SecondShot);
        const auto decision = SubmitGunImpl(cmd, local, configured, view, manual, retained, result);
        if (doubleTap.second && result.fired) {
            doubleTap.firstCommand = 0;
            dtState.store(DoubleTapState::Unavailable);
            dtProgress.store(0.f);
        }
        if (result.fired) {
            if (doubleTap.charged) {
                doubleTap.firstCommand = cmd->nCommandNumber;
                doubleTap.untilCommand = cmd->nCommandNumber + doubleTap.cycleCommands;
                dtState.store(DoubleTapState::SecondShot);
                dtProgress.store(0.f);
            } else
                doubleTap.historyTick = -1;
        } else
            doubleTap.historyTick = 0;
        Visualise::Update(cmd, local, configured, view, retained, result);
        RewriteDoubleTap(cmd);
        return decision;
    }
} // namespace Rage
