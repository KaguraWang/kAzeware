#include "target.h"
#include "motion.h"
#include "auto_aim.h"
#include "../legitbot/damage.h"
#include "../legitbot/hitboxes.h"
#include "../legitbot/ray.h"
#include "../gamemode/gamemode.h"
#include "../sdk_prio_a/sdk_prio_a.h"
#include "../trace/trace.h"
#include "../../interfaces/interfaces.h"
#include "../../../cs2/entity/CCSPlayerController/CCSPlayerController.h"
#include <limits>
#include <numbers>
#include <bit>
#include <optional>

namespace Rage {
    namespace {
        constexpr float degrees = 180.f / std::numbers::pi_v<float>;

        struct PoseSnapshot {
            Candidate pose{};
            Vector_t origin{};
        };

        std::array<PoseSnapshot, SdkPrioA::kMaxTrackedControllers> scanPoses{};
        int scanCommand = -1, scanCount = 0;
        C_CSPlayerPawn* scanLocal = nullptr;

        struct DamageKey {
            unsigned target = 0, weapon = 0;
            int armor = 0, team = 0, group = 0;
            bool wall = false;
            std::uintptr_t data = 0;
            std::array<float, 6> segment{};
            bool operator==(const DamageKey&) const = default;
        };

        struct CachedDamage {
            DamageKey key{};
            std::uint64_t epoch = 0;
            float damage = -1.f;
            bool clear = false;
        };

        std::array<CachedDamage, 4096> damageCache{};
        std::uint64_t scanEpoch = 1;

        std::size_t DamageSlot(const DamageKey& key) {
            std::uint32_t hash = key.target * 16777619u ^ key.weapon;
            for (float value : key.segment)
                hash = (hash ^ std::bit_cast<std::uint32_t>(value)) * 16777619u;
            hash ^= hash >> 16;
            hash *= 0x7feb352du;
            hash ^= hash >> 15;
            hash *= 0x846ca68bu;
            hash ^= hash >> 16;
            return hash & (damageCache.size() - 1);
        }

        Candidate CapturePose(C_CSPlayerPawn* pawn, const Vector_t& origin, bool refresh) {
            const auto handle = pawn->handle();
            const float simulation = pawn->m_flSimulationTime();
            const double poseTime = double(simulation) * 64.;
            if (!std::isfinite(poseTime) || poseTime <= 0. || poseTime >= INT_MAX - 1)
                return {};
            PoseSnapshot* saved = nullptr;
            if (scanCommand >= 0 && scanLocal) {
                for (int i = 0; i < scanCount; ++i)
                    if (scanPoses[i].pose.handle == handle) {
                        saved = &scanPoses[i];
                        break;
                    }
                if (saved && !refresh && saved->pose.boxCount > 0 &&
                    double(saved->pose.renderTick) + saved->pose.renderFraction == poseTime &&
                    saved->origin.x == origin.x && saved->origin.y == origin.y && saved->origin.z == origin.z)
                    return saved->pose;
                if (!saved && scanCount < int(scanPoses.size()))
                    saved = &scanPoses[scanCount++];
            }
            Candidate pose{};
            pose.handle = handle;
            pose.renderTick = int(poseTime);
            pose.renderFraction = float(poseTime - pose.renderTick);
            pose.simulationPose = true;
            pose.seedInset = SeedPoseInset(pawn->m_vecVelocity());
            pose.boxCount = Bones::CollectHitboxCapsulesReadonly(pawn, 0xff, pose.boxes.data(), int(pose.boxes.size()));
            if (pawn->m_flSimulationTime() != simulation)
                return {};
            if (saved)
                *saved = {pose, origin};
            return pose;
        }

        float AngularDistance(const Vector_t& delta, const QAngle_t& view) {
            Vector_t forward;
            view.ToDirections(&forward);
            const float length = delta.Length();
            return length > .001f ? std::acos(std::clamp(Legitbot::Dot(forward, delta) / length, -1.f, 1.f)) * degrees
                                  : 0.f;
        }

        bool Body(int group) {
            return group >= 2 && group <= 4;
        }

        RayIntersection Intersect(
            const Candidate& candidate, const Vector_t& eye, const Vector_t& ray, float range, float inset = 0.f) {
            // Native player clipping keeps the nearest hit in each damage group,
            // then prefers head, stomach, chest, neck, arms, generic and legs. Arms
            // in front of the torso do not make the server choose an arm hit.
            constexpr std::array<int, 8> priority{0, 3, 2, 1, 1, 4, 6, 6};
            std::array<RayIntersection, 7> hits{};
            std::array<const Bones::Capsule*, 7> capsules{};
            for (auto& hit : hits)
                hit.distance = range;
            for (int i = 0; i < candidate.boxCount; ++i) {
                const auto& box = candidate.boxes[i];
                const auto& capsule = box.capsule;
                const int group = box.group >= 0 && box.group < int(priority.size()) ? priority[box.group] : 5;
                const float at =
                    Legitbot::RayCapsule(eye, ray, hits[group].distance, capsule.mins, capsule.maxs, capsule.radius);
                if (at < range && at <= hits[group].distance) {
                    hits[group] = {at, box.group, capsule.studioIndex};
                    capsules[group] = &capsule;
                }
            }
            // Head priority does not shoot through a nearer stomach/chest hit.
            if (capsules[0] && (hits[1].distance < hits[0].distance || hits[2].distance < hits[0].distance))
                capsules[0] = nullptr;
            for (int group = 0; group < int(hits.size()); ++group)
                if (const auto* first = capsules[group]) {
                    if (inset > 0.f && (first->radius <= inset ||
                                        Legitbot::RayCapsule(
                                            eye, ray, range, first->mins, first->maxs, first->radius - inset) >= range))
                        return {range};
                    return hits[group];
                }
            return {range};
        }

        int FillPoints(const Bones::GroupCapsule& box,
                       const Vector_t& eye,
                       const Settings& settings,
                       bool centerVisible,
                       Vector_t (&points)[5]) {
            const auto& capsule = box.capsule;
            points[0] = capsule.center;
            if (centerVisible)
                return 1;
            const auto delta = capsule.center - eye;
            const float horizontal = std::hypot(delta.x, delta.y), length = delta.Length();
            if (horizontal <= .001f || length <= .001f)
                return 1;
            const Vector_t right{-delta.y / horizontal, delta.x / horizontal, 0};
            const Vector_t up{-delta.z * delta.x / (length * horizontal),
                              -delta.z * delta.y / (length * horizontal),
                              horizontal / length};
            const float scale = settings.PointScale(box.group);
            const float radius = scale == 0.f ? AutoAim::PointRadius(capsule.radius, length, settings.auto_point_cone)
                                              : capsule.radius * scale * .0095f;
            if (radius <= .001f)
                return 1;
            points[1] = capsule.center + right * radius;
            points[2] = capsule.center - right * radius;
            points[3] = capsule.center + up * radius;
            points[4] = capsule.center - up * radius;
            return 5;
        }

    } // namespace

    // 扫描缓存以命令和本地玩家为边界，避免在同一命令内重复采集姿态。
    void BeginTargetScan(int command, C_CSPlayerPawn* local) {
        if (scanCommand != command || scanLocal != local) {
            scanCommand = command;
            scanLocal = local;
            scanCount = 0;
            ++scanEpoch;
            BeginEnemyForecasts();
        }
    }

    void RefreshTargetPoses() {
        // Refinement must capture fresh bones, but the command's collision scene
        // has not advanced. A changed pose produces different ray endpoints;
        // identical physical rays can retain their damage/cover result.
        scanCount = 0;
    }

    void ResetTargetScan() {
        scanCommand = -1;
        scanLocal = nullptr;
        scanCount = 0;
        ++scanEpoch;
    }

    int RayStudio(const Candidate& candidate, const Vector_t& eye, const Vector_t& ray, float range) {
        return Intersect(candidate, eye, ray, range).studio;
    }

    float SeedPoseInset(const Vector_t& velocity) {
        const float speed = velocity.Length();
        return speed > 1.f ? (std::max)(2.f, speed / 128.f) : 0.f;
    }

    RayIntersection
    SeedIntersection(const Candidate& candidate, const Vector_t& eye, const Vector_t& ray, float range) {
        // Cover half a tick of motion and small animation differences without
        // changing the native damage-group selection.
        return Intersect(candidate, eye, ray, range, candidate.seedInset);
    }

    bool CenterVisible(C_CSPlayerPawn* local,
                       C_CSPlayerPawn* target,
                       const Candidate& candidate,
                       const Bones::Capsule& capsule,
                       const Vector_t& eye) {
        const auto delta = capsule.center - eye;
        if (RayStudio(candidate, eye, delta.Normalize(), delta.Length() + 1.f) != capsule.studioIndex)
            return false;
        Trace::CGameTrace trace{};
        return Trace::TraceHistoricalPath(eye, capsule.center, local, target, trace) && !Trace::DidHit(trace);
    }

    int AimPoints(C_CSPlayerPawn* local,
                  C_CSPlayerPawn* target,
                  const Candidate& candidate,
                  const Bones::GroupCapsule& box,
                  const Vector_t& eye,
                  const Settings& settings,
                  Vector_t (&points)[5]) {
        return FillPoints(box, eye, settings, CenterVisible(local, target, candidate, box.capsule, eye), points);
    }

    QAngle_t AimAngle(const Vector_t& eye, const Vector_t& point) {
        const auto delta = point - eye;
        return {
            -std::atan2(delta.z, std::hypot(delta.x, delta.y)) * degrees, std::atan2(delta.y, delta.x) * degrees, 0.f};
    }

    bool InAimCone(const Vector_t& delta, const QAngle_t& view, int fov) {
        if (fov >= 180)
            return true;
        return AngularDistance(delta, view) <= std::clamp(fov, 1, 180) + .0001f;
    }

    bool Prefer(const Candidate& a, const Candidate& b, const Settings& settings, CBaseHandle retained) {
        if (!a)
            return false;
        if (!b)
            return true;
        float x = 0, y = 0;
        switch (settings.priority) {
        case 1:
            x = -a.damage;
            y = -b.damage;
            break;
        case 2:
            x = float(a.health);
            y = float(b.health);
            break;
        case 3:
            x = a.distance;
            y = b.distance;
            break;
        case 4:
            x = -a.distance;
            y = -b.distance;
            break;
        default:
            x = a.angle;
            y = b.angle;
            break;
        }
        if (std::abs(x - y) > .001f)
            return x < y;
        if ((a.handle == retained) != (b.handle == retained))
            return a.handle == retained;
        return a.handle.raw() < b.handle.raw();
    }

    namespace {
        struct ScannedPoint {
            Vector_t point{};
            int group = -1, studio = -1, pointIndex = -1, health = 0;
            float damage = 0.f, angle = 0.f;
            bool shootable = false;

            explicit operator bool() const {
                return group >= 0;
            }
        };

        void TranslatePose(Candidate& pose, const Vector_t& offset) {
            if (offset.x == 0.f && offset.y == 0.f && offset.z == 0.f)
                return;
            for (int i = 0; i < pose.boxCount; ++i) {
                auto& box = pose.boxes[i].capsule;
                box.center += offset;
                box.mins += offset;
                box.maxs += offset;
            }
        }

        void ApplyPoint(Candidate& pose, const ScannedPoint& point) {
            pose.point = point.point;
            pose.group = point.group;
            pose.studio = point.studio;
            pose.pointIndex = point.pointIndex;
            pose.health = point.health;
            pose.damage = point.damage;
            pose.angle = point.angle;
            pose.shootable = point.shootable;
        }

        ScannedPoint ScanPose(C_CSPlayerPawn* local,
                              C_CSWeaponBase* weapon,
                              const Candidate& pose,
                              const Vector_t& eye,
                              const QAngle_t& view,
                              const Settings& settings,
                              bool firstEligible,
                              bool preparation,
                              ScanAcceptance acceptance,
                              const Vector_t& offset,
                              ScanCursor* cursor = nullptr) {
            ScannedPoint best{}, potential{};
            if (!local || !weapon || !pose.handle.valid() || !pose.boxCount || !I::GameEntity ||
                !I::GameEntity->Instance)
                return best;
            auto* target = I::GameEntity->Instance->Get<C_CSPlayerPawn>(pose.handle);
            auto* data = weapon->Data();
            if (!target || !data || target->m_iHealth() <= 0 || target->m_lifeState() != 0 ||
                target->m_bGunGameImmunity())
                return best;
            auto pointSettings = settings;
            if (acceptance.lastPoint > 1 && settings.AutoMultipoint() && settings.auto_point_cone < 0.f)
                pointSettings.auto_point_cone = AutoPointCone(
                    Legitbot::ReadFireAccuracy(local, weapon), settings.auto_multipoint_mode, settings.hc_budget);
            const auto relativeEye = eye - offset;
            const unsigned mask = settings.HitboxMask();
            const int health = target->m_iHealth();
            const float minimum = (std::max)(float(settings.Minimum(health)), acceptance.damageFloor);
            const int pellets = std::clamp(data->m_nNumBullets(), 1, 16);
            const float range = data->m_flRange();

            struct BoxWork {
                int index;
                float upper;
            };

            std::array<BoxWork, 32> work{};
            int count = 0;
            for (int i = 0; i < pose.boxCount; ++i) {
                const auto& box = pose.boxes[i];
                if (box.group < 0 || !(mask & (1u << box.group)))
                    continue;
                const auto delta = box.capsule.center - relativeEye;
                const float distance = delta.Length();
                // Bound the nearest MP entry, not the farther center, at MD cutoffs.
                const float radius = box.capsule.radius + (box.capsule.maxs - box.capsule.mins).Length() * .5f;
                if (distance - radius > range)
                    continue;
                const auto nearPoint = eye + delta.Normalize() * (std::max)(1.f, distance - radius);
                const float upper = Legitbot::ExposedDamage(eye, nearPoint, box.group, target, weapon) * pellets;
                if (upper >= minimum)
                    work[count++] = {i, upper};
            }
            std::sort(work.begin(), work.begin() + count, [&](const auto& a, const auto& b) {
                const bool bodyA = Body(pose.boxes[a.index].group), bodyB = Body(pose.boxes[b.index].group);
                if (settings.body_aim == 1 && bodyA != bodyB)
                    return bodyA;
                return a.upper > b.upper || (a.upper == b.upper && a.index < b.index);
            });
            ScannedPoint trial{};
            trial.health = health;
            std::optional<Candidate> accepted;
            const auto preferPoint = [&](const ScannedPoint& a, const ScannedPoint& b) {
                if (!b)
                    return true;
                if (settings.body_aim == 1 && Body(a.group) != Body(b.group))
                    return Body(a.group);
                return a.damage > b.damage + .01f || (std::abs(a.damage - b.damage) <= .01f && a.angle < b.angle);
            };
            if (cursor && cursor->pawn != pose.handle) {
                cursor->pawn = pose.handle;
                cursor->box = cursor->point = 0;
            }
            const int begin = cursor && cursor->box < count ? cursor->box : 0;
            for (int wi = begin; wi < count; ++wi) {
                const auto& box = pose.boxes[work[wi].index];
                if (best && settings.body_aim == 1 && Body(best.group) && !Body(box.group))
                    continue;
                if (best && !(settings.body_aim == 1 && Body(box.group) && !Body(best.group)) &&
                    work[wi].upper + .01f < best.damage)
                    continue;
                Vector_t points[5]{};
                int pointCount = FillPoints(box, relativeEye, pointSettings, acceptance.lastPoint == 1, points);
                pointCount = (std::min)(pointCount, acceptance.lastPoint);
                for (int pi = (std::max)(acceptance.firstPoint,
                                         cursor && wi == begin ? (std::min)(cursor->point, pointCount - 1) : 0);
                     pi < pointCount;
                     ++pi) {
                    if (cursor) {
                        cursor->box = wi;
                        cursor->point = pi;
                    }
                    if (acceptance.budget && acceptance.budget->Expired())
                        return best;
                    const auto delta = points[pi] - relativeEye;
                    const float angle = AngularDistance(delta, view);
                    if (angle > settings.fov + .0001f || angle > acceptance.angleCeiling)
                        continue;
                    const auto ray = delta.Normalize();
                    const auto hit = Intersect(pose, relativeEye, ray, range);
                    if (hit.studio != box.capsule.studioIndex)
                        continue;
                    const float exposed =
                        Legitbot::ExposedDamage(eye, eye + ray * hit.distance, hit.group, target, weapon) * pellets;
                    if (exposed < minimum)
                        continue;
                    bool unobstructed = false;
                    const float damage =
                        RayDamage(
                            local, weapon, pose, eye, ray, settings, float(minimum) / pellets, &hit, &unobstructed) *
                        pellets;
                    if (pi == 0 && unobstructed)
                        pointCount = 1;
                    trial.point = points[pi] + offset;
                    trial.pointIndex = pi;
                    trial.group = box.group;
                    trial.studio = hit.studio;
                    trial.angle = angle;
                    trial.shootable = damage >= minimum;
                    trial.damage = trial.shootable ? damage : exposed;
                    if (!trial.shootable) {
                        if (preparation && preferPoint(trial, potential))
                            potential = trial;
                        continue;
                    }
                    if (!firstEligible && !preferPoint(trial, best))
                        continue;
                    if (acceptance.test) {
                        if (!accepted) {
                            accepted.emplace(pose);
                            TranslatePose(*accepted, offset);
                        }
                        ApplyPoint(*accepted, trial);
                        if (!acceptance.test(acceptance.context, *accepted))
                            continue;
                    }
                    // Damage priority needs the best damage point. Other target
                    // priorities can use this fully accepted point immediately;
                    // scanning more MP edges must not postpone an eligible shot.
                    if (firstEligible || (acceptance.test && !preparation && settings.priority != 1))
                        return trial;
                    best = trial;
                    // A sufficient accepted center keeps priority over its MP edges.
                    if (pi == 0)
                        break;
                }
            }
            if (cursor)
                cursor->box = cursor->point = 0;
            return best ? best : potential;
        }
    } // namespace

    Candidate ScanCandidate(C_CSPlayerPawn* local,
                            C_CSWeaponBase* weapon,
                            const Candidate& pose,
                            const Vector_t& eye,
                            const QAngle_t& view,
                            const Settings& settings,
                            bool firstEligible,
                            bool preparation,
                            ScanAcceptance acceptance,
                            const Vector_t& offset,
                            ScanCursor* cursor) {
        const auto point = ScanPose(
            local, weapon, pose, eye, view, settings, firstEligible || cursor, preparation, acceptance, offset, cursor);
        if (!point)
            return {};
        auto result = pose;
        TranslatePose(result, offset);
        ApplyPoint(result, point);
        return result;
    }

    bool HasDamage(C_CSPlayerPawn* local,
                   C_CSWeaponBase* weapon,
                   const Candidate& pose,
                   const Vector_t& eye,
                   const QAngle_t& view,
                   const Settings& settings,
                   const Vector_t& offset,
                   ScanBudget* budget) {
        return bool(
            ScanPose(local, weapon, pose, eye, view, settings, true, false, {nullptr, nullptr, 0.f, budget}, offset));
    }

    Candidate SelectCandidate(C_CSPlayerPawn* local,
                              C_CSWeaponBase* weapon,
                              const Vector_t& eye,
                              const QAngle_t& view,
                              const Settings& settings,
                              CBaseHandle retained,
                              bool preparation,
                              bool retainedOnly,
                              ScanAcceptance acceptance,
                              ScanCursor* cursor,
                              int forecastTicks,
                              double extrapolationTime) {
        Candidate best{};
        const unsigned mask = settings.HitboxMask();
        if (!local || !weapon || !mask || !Bones::IsValidPos(eye) || !I::GameEntity || !I::GameEntity->Instance)
            return best;
        auto* data = weapon->Data();
        if (!data)
            return best;
        const float range = data->m_flRange();
        const bool teamFilter = GameMode::WantTeamCheck(true);
        int controllers[SdkPrioA::kMaxTrackedControllers]{};
        const int count = SdkPrioA::CopyControllerIndices(controllers, SdkPrioA::kMaxTrackedControllers);
        const int begin = cursor && cursor->target < count ? cursor->target : 0;
        for (int ci = begin; ci < count; ++ci) {
            if (cursor)
                cursor->target = ci;
            if (cursor && acceptance.budget && acceptance.budget->Expired())
                return {};
            auto* controller = I::GameEntity->Instance->Get<CCSPlayerController>(controllers[ci]);
            if (!controller || !controller->m_bPawnIsAlive())
                continue;
            const auto handle = controller->m_hPlayerPawn();
            auto* target = I::GameEntity->Instance->Get<C_CSPlayerPawn>(handle);
            if (retainedOnly && handle != retained)
                continue;
            if (!target || target == local || target->m_iHealth() <= 0 || target->m_lifeState() != 0 ||
                target->m_bGunGameImmunity())
                continue;
            if (teamFilter && target->m_iTeamNum() == local->m_iTeamNum())
                continue;
            auto* node = target->m_pGameSceneNode();
            if (!node || node->m_bDormant())
                continue;
            const auto origin = node->m_vecAbsOrigin();
            const auto center = origin + Vector_t{0, 0, 36};
            const float distance = (center - eye).Length();
            const bool predicted = forecastTicks > 0 || extrapolationTime > 0.;
            if (!predicted && distance > range + 100.f)
                continue;
            if (!predicted && distance > 100.f &&
                AngularDistance(center - eye, view) > settings.fov + std::asin(100.f / distance) * degrees)
                continue;
            if (!predicted && acceptance.test && !preparation && best) {
                // These priorities do not depend on the as-yet untraced point.
                // Ties still reach Prefer(), including retained-target handling.
                if (settings.priority == 2 && target->m_iHealth() > best.health)
                    continue;
                if (settings.priority == 3 && distance > best.distance + .001f)
                    continue;
                if (settings.priority == 4 && distance < best.distance - .001f)
                    continue;
            }
            // The final refine-shot call deliberately requests a new pose even
            // within this command. Preparation and ordinary selection share one.
            auto pose = CapturePose(target, origin, retainedOnly);
            pose.distance = distance;
            const double poseTime = double(pose.renderTick) + pose.renderFraction;
            const double gap = extrapolationTime - poseTime;
            // Fill only the short interval to the command's shot clock. Never add
            // ping or weapon cooldown on top of it, or extend a long outage.
            const int missingTicks = std::isfinite(gap) && gap >= 1. && gap <= 8.
                                         ? (std::min)(int(std::floor(gap)), settings.extrapolation_ticks)
                                         : 0;
            const int ticks = missingTicks + forecastTicks;
            if (ticks > 0 && pose.boxCount > 0) {
                const float simulation = target->m_flSimulationTime();
                const TargetPose source{
                    pose, origin, target->m_vecVelocity(), simulation, (target->m_fFlags() & FL_ONGROUND) != 0};
                Vector_t offset{};
                if (!ForecastEnemyOffset(source, ticks, offset))
                    continue;
                TranslatePose(pose, offset);
                pose.distance = (center + offset - eye).Length();
                pose.renderTick += ticks;
                pose.extrapolated = true;
                pose.sourceSimulationTime = simulation;
                pose.forecastOffset = offset;
            }
            auto filter = acceptance;

            struct RankedAcceptance {
                ScanAcceptance original;
                const Candidate* best;
                const Settings* settings;
                CBaseHandle retained;
            };

            RankedAcceptance ranked{acceptance, &best, &settings, retained};
            if (acceptance.test && !preparation) {
                if (best && settings.priority == 0)
                    filter.angleCeiling = (std::min)(filter.angleCeiling, best.angle + .001f);
                filter.context = &ranked;
                filter.test = [](void* context, const Candidate& candidate) {
                    const auto& q = *static_cast<const RankedAcceptance*>(context);
                    return Prefer(candidate, *q.best, *q.settings, q.retained) &&
                           q.original.test(q.original.context, candidate);
                };
            }
            if (best && best.shootable && settings.priority == 1 && data->m_nNumBullets() == 1)
                filter.damageFloor = (std::max)(filter.damageFloor, best.damage - .01f);
            const auto candidate = ScanCandidate(
                local, weapon, pose, eye, view, settings, cursor != nullptr, preparation, filter, {}, cursor);
            if (cursor) {
                if (candidate)
                    return candidate;
                if (acceptance.budget && acceptance.budget->exhausted)
                    return {};
            }
            if (preparation && candidate && best && candidate.shootable != best.shootable) {
                if (candidate.shootable)
                    best = candidate;
            } else if (Prefer(candidate, best, settings, retained))
                best = candidate;
        }
        if (cursor)
            *cursor = {};
        return best;
    }

    int CollectTargetPoses(C_CSPlayerPawn* local,
                           C_CSWeaponBase* weapon,
                           const Vector_t& eye,
                           const QAngle_t& view,
                           const Settings& settings,
                           std::span<TargetPose> output,
                           float extraRange) {
        if (!local || !weapon || !I::GameEntity || !I::GameEntity->Instance || output.empty())
            return 0;
        auto* data = weapon->Data();
        if (!data)
            return 0;
        const bool teams = GameMode::WantTeamCheck(true);
        const float allowance = 100.f + (std::max)(0.f, extraRange);
        int indices[SdkPrioA::kMaxTrackedControllers]{};
        const int count = SdkPrioA::CopyControllerIndices(indices, SdkPrioA::kMaxTrackedControllers);
        int written = 0;
        for (int i = 0; i < count && written < int(output.size()); ++i) {
            auto* controller = I::GameEntity->Instance->Get<CCSPlayerController>(indices[i]);
            if (!controller || !controller->m_bPawnIsAlive())
                continue;
            auto* pawn = I::GameEntity->Instance->Get<C_CSPlayerPawn>(controller->m_hPlayerPawn());
            if (!pawn || pawn == local || pawn->m_iHealth() <= 0 || pawn->m_lifeState() != 0 ||
                pawn->m_bGunGameImmunity() || (teams && pawn->m_iTeamNum() == local->m_iTeamNum()))
                continue;
            auto* node = pawn->m_pGameSceneNode();
            if (!node || node->m_bDormant())
                continue;
            const auto origin = node->m_vecAbsOrigin(), delta = origin + Vector_t{0, 0, 36} - eye;
            const float distance = delta.Length();
            if (distance > data->m_flRange() + allowance)
                continue;
            if (distance > allowance &&
                AngularDistance(delta, view) > settings.fov + std::asin(allowance / distance) * degrees)
                continue;
            auto pose = CapturePose(pawn, origin, false);
            if (!pose.boxCount)
                continue;
            pose.health = pawn->m_iHealth();
            pose.distance = distance;
            output[written++] = {
                pose, origin, pawn->m_vecVelocity(), pawn->m_flSimulationTime(), (pawn->m_fFlags() & FL_ONGROUND) != 0};
        }
        return written;
    }

    float RayDamageUpperBound(C_CSWeaponBase* weapon,
                              const Candidate& candidate,
                              const Vector_t& eye,
                              const Vector_t& ray,
                              const Settings& settings,
                              RayIntersection* intersection) {
        if (intersection)
            *intersection = {};
        if (!weapon || !I::GameEntity || !I::GameEntity->Instance)
            return -1.f;
        auto* target = I::GameEntity->Instance->Get<C_CSPlayerPawn>(candidate.handle);
        auto* data = weapon->Data();
        if (!target || !data)
            return -1.f;
        const auto hit = Intersect(candidate, eye, ray, data->m_flRange());
        if (intersection)
            *intersection = hit;
        if (hit.group < 0 || !(settings.HitboxMask() & (1u << hit.group)))
            return -1.f;
        return Legitbot::ExposedDamage(eye, eye + ray * hit.distance, hit.group, target, weapon);
    }

    float RayDamage(C_CSPlayerPawn* local,
                    C_CSWeaponBase* weapon,
                    const Candidate& candidate,
                    const Vector_t& eye,
                    const Vector_t& ray,
                    const Settings& settings,
                    float minimumDamage,
                    const RayIntersection* cachedIntersection,
                    bool* unobstructed) {
        if (unobstructed)
            *unobstructed = false;
        if (!candidate.handle.valid() || !weapon || !I::GameEntity || !I::GameEntity->Instance)
            return -1.f;
        auto* target = I::GameEntity->Instance->Get<C_CSPlayerPawn>(candidate.handle);
        auto* data = weapon->Data();
        if (!target || !data || target->m_iHealth() <= 0)
            return -1.f;
        const auto intersection =
            cachedIntersection ? *cachedIntersection : Intersect(candidate, eye, ray, data->m_flRange());
        if (intersection.group < 0 || !(settings.HitboxMask() & (1u << intersection.group)))
            return -1.f;
        const auto hit = eye + ray * (intersection.distance + .01f);
        // Selection and HC must test the same captured pose. The live physics
        // collider can be from a different interpolation update, even without BT.
        if (minimumDamage > 0.f &&
            Legitbot::ExposedDamage(eye, hit, intersection.group, target, weapon) + .01f < minimumDamage)
            return -1.f;
        // During one command the scene is unchanged. Forecast branches often ask
        // the exact same segment again; share its result, including cover misses.
        // Geometry, live health and the MD upper-bound rejection above still run.
        const DamageKey key{candidate.handle.raw(),
                            reinterpret_cast<CEntityInstance*>(weapon)->handle().raw(),
                            target->m_ArmorValue(),
                            local->m_iTeamNum(),
                            intersection.group,
                            settings.autowall,
                            reinterpret_cast<std::uintptr_t>(data),
                            {eye.x, eye.y, eye.z, hit.x, hit.y, hit.z}};
        auto& cached = damageCache[DamageSlot(key)];
        const bool cacheable = scanCommand >= 0 && scanLocal == local;
        if (cacheable && cached.epoch == scanEpoch && cached.key == key) {
            if (unobstructed)
                *unobstructed = cached.clear;
            return cached.damage;
        }
        bool clear = false;
        const auto result =
            Legitbot::TraceShot(eye, hit, intersection.group, local, target, weapon, true, settings.autowall, &clear);
        if (cacheable)
            cached = {key, scanEpoch, result.damage, clear};
        if (unobstructed)
            *unobstructed = clear;
        return result.damage;
    }
} // namespace Rage
