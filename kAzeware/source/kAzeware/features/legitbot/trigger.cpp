#include "legit_math.h"
#include "trigger.h"
#include "ray.h"
#include "accuracy.h"
#include "smoke.h"
#include "hitboxes.h"
#include "damage.h"
#include "profiles.h"
#include "trigger_timing.h"
#include "../input_inject/input_inject.h"
#include "../movement/velocity_movement.h"
#include "../movement/peek_assist.h"
#include "../../config/config.h"
#include "../../interfaces/CUserCmd/CUserCmd.h"
#include "../../utils/memory/memsafe/memsafe.h"
#include "../../utils/schema/schema.h"
#include <Windows.h>
#include <atomic>
#include "../bones/bones.h"
#include "../trace/trace.h"
#include "../gamemode/gamemode.h"
#include "../sdk_prio_a/sdk_prio_a.h"
#include "../../interfaces/interfaces.h"
#include "../../../cs2/entity/CCSPlayerController/CCSPlayerController.h"
#include <cmath>
#include <numbers>
#include <limits>

namespace Legitbot {
    namespace {
        TriggerScheduler scheduler;
        std::atomic<std::uint64_t> localShots{0}, lastShotAt{0}, killGeneration{0}, killShots{0};
        std::atomic<std::uint32_t> killWeapon{0xffffffffu};
        std::uint64_t seenKills = 0, overshootStart = 0, overshootSent = 0;
        std::uint32_t overshootWeapon = 0xffffffffu;
        int lastCommand = 0, lastFireCommand = 0, overshootCount = 0;
        bool overshootWaiting = false;

        void Fire(CUserCmd* cmd) {
            const int count = cmd->csgoUserCmd.inputHistoryField.nCurrentSize;
            if (count > 0 && count <= 128 && cmd->GetInputHistoryEntry(count - 1)) {
                cmd->csgoUserCmd.nAttack1StartHistoryIndex = count - 1;
                cmd->csgoUserCmd.nHasBits |= CSGOUSERCMD_BITS_ATTACK1START;
            }
            InputInject::SetEdge(cmd, IN_ATTACK, true);
            PeekAssist::OnShotCommand(cmd);
            lastFireCommand = cmd->nCommandNumber;
        }
    } // namespace

    void ResetTrigger() {
        scheduler.Reset();
        lastCommand = lastFireCommand = 0;
        overshootCount = 0;
        overshootWaiting = false;
        seenKills = killGeneration.load(std::memory_order_acquire);
    }

    float TriggerReadyIn(CBaseHandle target, float acquisitionIn) {
        const auto& c = LegitProfiles::Current();
        const auto now = GetTickCount64();
        scheduler.Observe(
            now, localShots.load(std::memory_order_acquire), c.trigger_delay, c.cooldown, c.burst ? c.burst_shots : 1);
        return (scheduler.ForecastReadyAt(now,
                                          target.raw(),
                                          now + static_cast<std::uint64_t>(std::ceil(acquisitionIn * 1000.f)),
                                          c.trigger_delay,
                                          c.first_shot_delay) -
                now) *
               .001f;
    }

    void NoteLocalShot() {
        localShots.fetch_add(1, std::memory_order_relaxed);
        lastShotAt.store(GetTickCount64(), std::memory_order_release);
    }

    void NoteLocalKill(C_CSPlayerPawn* local) {
        const auto now = GetTickCount64(), shotAt = lastShotAt.load(std::memory_order_acquire);
        auto* services = local ? local->GetWeaponServices() : nullptr;
        if (!services || !shotAt || now - shotAt > 2000)
            return;
        killWeapon.store(services->m_hActiveWeapon().raw(), std::memory_order_relaxed);
        killShots.store(localShots.load(std::memory_order_relaxed), std::memory_order_relaxed);
        killGeneration.fetch_add(1, std::memory_order_release);
    }

    void ReplayTrigger(CUserCmd* cmd) {
        auto& config = LegitProfiles::Current();
        if (cmd && config.enabled && (config.trigger || overshootCount > 0) && cmd->nCommandNumber &&
            cmd->nCommandNumber == lastCommand && lastFireCommand == lastCommand)
            Fire(cmd);
    }

    TriggerCandidate FindTriggerTarget(C_CSPlayerPawn* local,
                                       const QAngle_t& view,
                                       float range,
                                       unsigned hitgroups,
                                       bool teamCheck,
                                       bool visibleCheck,
                                       bool smokeCheck,
                                       bool multipoint,
                                       const float* pointScales,
                                       const SeedShot* seed,
                                       const AccuracyState* accuracy) {
        TriggerCandidate result{};
        if (!local || !hitgroups || range <= 0.f || !I::GameEntity || !I::GameEntity->Instance)
            return result;
        const Vector_t eye = seed ? seed->eye : Bones::GetEyePos(local);
        if (!Bones::IsValidPos(eye))
            return result;
        constexpr float radians = std::numbers::pi_v<float> / 180.f;
        const float pitch = view.x * radians, yaw = view.y * radians;
        const Vector_t direction{std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), -std::sin(pitch)};
        Vector_t rays[16]{direction};
        int rayCount = 1;
        if (seed) {
            if (!accuracy || !SeedDirections(seed->angle, seed->tick, *accuracy, rays))
                return result;
            rayCount = accuracy->bullets;
        }
        const bool filterTeam = teamCheck && GameMode::WantTeamCheck(true);
        const int team = local->m_iTeamNum();
        int indices[SdkPrioA::kMaxTrackedControllers]{};
        const int count = SdkPrioA::CopyControllerIndices(indices, SdkPrioA::kMaxTrackedControllers);
        float nearest = std::numeric_limits<float>::infinity();
        for (int i = 0; i < count; ++i) {
            auto* controller = I::GameEntity->Instance->Get<CCSPlayerController>(indices[i]);
            if (!controller || !controller->m_bPawnIsAlive())
                continue;
            const CBaseHandle handle = controller->m_hPlayerPawn();
            auto* pawn = I::GameEntity->Instance->Get<C_CSPlayerPawn>(handle);
            if (!pawn || pawn == local || pawn->m_iHealth() <= 0 || pawn->m_lifeState() != 0 ||
                pawn->m_bGunGameImmunity())
                continue;
            if (filterTeam && pawn->m_iTeamNum() == team)
                continue;
            auto* node = pawn->m_pGameSceneNode();
            if (!node || node->m_bDormant())
                continue;
            const Vector_t center = node->m_vecAbsOrigin() + Vector_t{0.f, 0.f, 36.f};
            bool reachable = false;
            for (int r = 0; r < rayCount; r++)
                reachable |= std::isfinite(RayCapsule(eye, rays[r], range, center, center, 100.f));
            if (!reachable)
                continue;
            Bones::GroupCapsule capsules[32]{};
            const int n = ReadHitboxes(pawn, handle, hitgroups, capsules, 32);
            float totalDamage = 0, closest = range;
            TriggerCandidate candidate{};
            for (int r = 0; r < rayCount; r++) {
                const auto direction = rays[r];
                float pelletDistance = range;
                Vector_t pelletPoint{};
                int pelletGroup = -1;
                for (int j = 0; j < n; ++j) {
                    const auto& box = capsules[j].capsule;
                    const float scale = pointScales[capsules[j].group];
                    const float centerRadius = TriggerCenterRadius(box.radius, multipoint, scale);
                    if (!std::isfinite(RayCapsule(eye, direction, range, box.center, box.center, centerRadius)))
                        continue;
                    const float distance = RayCapsule(eye, direction, range, box.mins, box.maxs, box.radius);
                    if (!std::isfinite(distance) || distance >= pelletDistance)
                        continue;
                    const Vector_t point = eye + direction * (distance + .01f);
                    if (smokeCheck && SmokeBlocks(eye, point))
                        continue;
                    if (visibleCheck && !Visible(eye, point, local, pawn))
                        continue;
                    pelletDistance = distance;
                    pelletPoint = point;
                    pelletGroup = capsules[j].group;
                }
                if (pelletGroup < 0)
                    continue;
                if (seed)
                    totalDamage +=
                        (std::max)(0.f, ShotDamage(eye, pelletPoint, pelletGroup, local, pawn, accuracy->weapon));
                if (pelletDistance < closest) {
                    closest = pelletDistance;
                    candidate.handle = handle;
                    candidate.hitPoint = pelletPoint;
                    candidate.group = pelletGroup;
                }
            }
            if (seed &&
                totalDamage <
                    (std::max)(1, LegitMath::MinimumDamage(LegitProfiles::Current().min_damage, pawn->m_iHealth())))
                continue;
            if (candidate && closest < nearest) {
                nearest = closest;
                result = candidate;
                result.damage = totalDamage;
            }
        }
        return result;
    }

    void RunTrigger(CUserCmd* cmd, C_CSPlayerPawn* local, bool weaponSwitching) {
        auto& c = LegitProfiles::Current();
        if (!cmd || !local || SdkPrioA::ShouldSoftDisableCombat() || !c.enabled) {
            ResetTrigger();
            return;
        }
        if (cmd->nCommandNumber && cmd->nCommandNumber == lastCommand) {
            ReplayTrigger(cmd);
            return;
        }
        lastCommand = cmd->nCommandNumber;
        lastFireCommand = 0;
        auto* base = cmd->csgoUserCmd.pBaseCmd;
        auto* weapon = local->GetActiveWeapon();
        auto* services = local->GetWeaponServices();
        if (!base || !base->pViewAngles || !weapon || !services || weapon->IsNonGunWeapon() ||
            weapon->m_iClip1() <= 0) {
            scheduler.Reset();
            overshootCount = 0;
            return;
        }
        auto* data = weapon->Data();
        const auto kills = killGeneration.load(std::memory_order_acquire);
        if (c.seed_trigger) {
            overshootCount = 0;
            overshootWaiting = false;
        }
        if (kills != seenKills) {
            seenKills = kills;
            overshootStart = killShots.load(std::memory_order_relaxed);
            overshootWeapon = killWeapon.load(std::memory_order_relaxed);
            overshootCount = c.seed_trigger ? 0 : std::clamp(c.overshoot_bullets, 0, 10);
            overshootWaiting = false;
        }
        if (overshootCount > 0) {
            const auto shots = localShots.load(std::memory_order_relaxed);
            if (!data || !data->m_bIsFullAuto() || services->m_hActiveWeapon().raw() != overshootWeapon ||
                weapon->m_bInReload() || weaponSwitching ||
                shots - overshootStart >= static_cast<std::uint64_t>(overshootCount)) {
                overshootCount = 0;
                overshootWaiting = false;
            } else {
                scheduler.Reset();
                if (cmd->nButtons.nValue & IN_ATTACK)
                    return;
                if (overshootWaiting && shots == overshootSent)
                    return;
                overshootWaiting = false;
                auto* controller = I::GameEntity && I::GameEntity->Instance
                                       ? I::GameEntity->Instance->Get<CCSPlayerController>(local->m_hController())
                                       : nullptr;
                if (controller && weapon->m_nNextPrimaryAttackTick() <= static_cast<int>(controller->m_nTickBase())) {
                    Fire(cmd);
                    overshootSent = shots;
                    overshootWaiting = true;
                }
                return;
            }
        }
        if (!c.trigger) {
            scheduler.Reset();
            return;
        }
        const bool burst = c.burst;
        const int burstCount = burst ? std::clamp(c.burst_shots, 0, 10) : 1;
        const auto shots = localShots.load(std::memory_order_relaxed);
        scheduler.Observe(GetTickCount64(), shots, c.trigger_delay, c.cooldown, burstCount);
        if (cmd->nButtons.nValue & IN_ATTACK)
            return;
        if (!data || (c.scoped_only && data->m_WeaponType() == 5 && !local->m_bIsScoped()) ||
            (c.trigger_checks[1] && !(local->m_fFlags() & FL_ONGROUND)) ||
            (c.trigger_checks[2] && local->m_flFlashOverlayAlpha() > .55f) ||
            (c.trigger_checks[3] && weapon->m_bInReload()) || (c.trigger_checks[4] && weaponSwitching)) {
            scheduler.LoseTarget();
            return;
        }
        if (c.VelocityCheckEnabled()) {
            const auto speedOffset = SchemaFinder::Get(hash_32_fnv1a_const("CCSWeaponBaseVData->m_flMaxSpeed"));
            float maxSpeed = 0.f;
            const int mode = std::clamp(weapon->m_weaponMode(), 0, 1);
            if (!speedOffset || !Mem::ReadField(data, speedOffset + mode * sizeof(float), maxSpeed) ||
                !std::isfinite(maxSpeed) || maxSpeed <= 0.f) {
                scheduler.LoseTarget();
                return;
            }
            const auto velocity = local->m_vecVelocity();
            const float limit = std::clamp(c.velocity_check, 0.f, 100.f) * .0035f * maxSpeed;
            if ((!std::isfinite(velocity.x) || !std::isfinite(velocity.y) ||
                 std::hypot(velocity.x, velocity.y) > limit)) {
                scheduler.LoseTarget();
                return;
            }
        }
        const float range = data->m_flRange();
        if (!std::isfinite(range) || range <= 0.f || range > 100000.f) {
            scheduler.LoseTarget();
            return;
        }
        QAngle_t view = base->pViewAngles->angValue, recoil{};
        if (!view.IsValid() || !Velocity::ReadWeaponRecoil(local, recoil)) {
            scheduler.LoseTarget();
            return;
        }
        view.x += recoil.x;
        view.y += recoil.y;
        unsigned groups = 0;
        for (int i = 0; i < 8; ++i)
            if (c.TriggerHitgroup(i))
                groups |= 1u << i;
        const bool smoke = c.trigger_checks[5];
        if (smoke && !UpdateSmoke()) {
            scheduler.LoseTarget();
            return;
        }
        std::array<float, 8> scales{};
        for (int i = 0; i < 8; ++i)
            scales[i] = c.TriggerPointScale(i);
        SeedShot shot{};
        AccuracyState accuracy{};
        float lowerAccuracy = 0.f, upperAccuracy = 0.f;
        if (c.seed_trigger) {
            accuracy = ReadFireAccuracy(local, weapon, &lowerAccuracy, &upperAccuracy);
            if (!accuracy.valid || !ReadSeedShot(cmd, local, weapon, shot)) {
                scheduler.LoseTarget();
                return;
            }
        }
        const auto candidate = FindTriggerTarget(local,
                                                 view,
                                                 range,
                                                 groups,
                                                 c.trigger_checks[6],
                                                 c.trigger_checks[0],
                                                 smoke,
                                                 c.TriggerMultipointEnabled(),
                                                 scales.data(),
                                                 c.seed_trigger ? &shot : nullptr,
                                                 c.seed_trigger ? &accuracy : nullptr);
        if (!candidate) {
            scheduler.LoseTarget();
            return;
        }
        auto* controller = I::GameEntity->Instance->Get<CCSPlayerController>(local->m_hController());
        auto* target = I::GameEntity->Instance->Get<C_CSPlayerPawn>(candidate.handle);
        if (!controller || !target) {
            scheduler.LoseTarget();
            return;
        }
        if (burstCount == 0) {
            scheduler.Reset();
            return;
        }
        const bool ready = weapon->m_nNextPrimaryAttackTick() <= static_cast<int>(controller->m_nTickBase());
        if (!scheduler.Ready(GetTickCount64(),
                             candidate.handle.raw(),
                             shots,
                             c.trigger_delay,
                             c.first_shot_delay,
                             c.cooldown,
                             burstCount,
                             ready))
            return;
        const float damage =
            c.seed_trigger
                ? candidate.damage
                : ShotDamage(Bones::GetEyePos(local), candidate.hitPoint, candidate.group, local, target, weapon);
        const int minimum = LegitMath::MinimumDamage(c.min_damage, target->m_iHealth());
        if (damage < 1.f || damage < static_cast<float>(minimum))
            return;
        if (c.seed_trigger)
            for (const float value : {lowerAccuracy, upperAccuracy}) {
                if (std::abs(value - accuracy.inaccuracy) < 1e-7f)
                    continue;
                auto boundary = accuracy;
                boundary.inaccuracy = value;
                const auto checked = FindTriggerTarget(local,
                                                       view,
                                                       range,
                                                       groups,
                                                       c.trigger_checks[6],
                                                       c.trigger_checks[0],
                                                       smoke,
                                                       c.TriggerMultipointEnabled(),
                                                       scales.data(),
                                                       &shot,
                                                       &boundary);
                if (!checked || checked.handle != candidate.handle || checked.group != candidate.group ||
                    checked.damage < (std::max)(1, minimum))
                    return;
            }
        const float hitchance = c.RequiredHitchance();
        if (hitchance > 0.f) {
            Bones::GroupCapsule boxes[32]{};
            const int n = ReadHitboxes(target, candidate.handle, groups, boxes, 32);
            if (!PassesHC(Bones::GetEyePos(local),
                          view,
                          range,
                          ReadAccuracy(weapon),
                          boxes,
                          n,
                          hitchance,
                          local,
                          target,
                          minimum,
                          c.hc_budget))
                return;
        }
        Fire(cmd);
        scheduler.ShotRequested(shots, GetTickCount64());
    }

} // namespace Legitbot
