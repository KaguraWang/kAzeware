#include "melee.h"
#include "../hitlog/hitlog.h"
#include "angles.h"
#include "profiles.h"
#include "../../config/config.h"
#include "../../interfaces/CCSGOInput/CCSGOInput.h"
#include "../input_inject/input_inject.h"
#include "../legitbot/accuracy.h"
#include "../legitbot/damage.h"
#include "../legitbot/hitboxes.h"
#include "../legitbot/ray.h"
#include "../gamemode/gamemode.h"
#include "../sdk_prio_a/sdk_prio_a.h"
#include "../trace/trace.h"
#include "../../interfaces/CUserCmd/CUserCmd.h"
#include "../../interfaces/interfaces.h"
#include "../../../cs2/entity/CCSPlayerController/CCSPlayerController.h"
#include <limits>
#include "knife_plan.h"

namespace Rage {
    bool IsMeleeWeapon(C_CSWeaponBase* weapon) {
        auto* data = weapon ? weapon->Data() : nullptr;
        return data && (data->m_WeaponType() == 0 || weapon->m_iItemDefinitionIndex() == 31);
    }

    namespace {
        bool Reach(C_CSPlayerPawn* local,
                   C_CSPlayerPawn* target,
                   const Vector_t& eye,
                   const Vector_t& point,
                   float range,
                   bool knife,
                   bool secondary) {
            const auto direction = (point - eye).Normalize();
            Trace::CGameTrace trace{};
            if (knife)
                return Trace::TraceKnifeAttack(eye, direction, secondary, local, trace) &&
                       Trace::HitsTarget(trace.hit_entity(), target);
            if (Trace::TraceLine(eye, eye + direction * range, local, trace, Trace::kMaskShot, 3, 15) &&
                Trace::HitsTarget(trace.hit_entity(), target))
                return true;
            return false;
        }

        struct KnifeDoubleTap {
            CBaseHandle weapon{}, target{};
            int command = 0, historyTick = -1, firstCommand = 0, untilCommand = 0, health = 0, armor = 0, damage = 0;
            bool secondary = false;
            double firstReset = 0.;
            QAngle_t angle{};
        } knifeTap;

        DoubleTapStatus knifeStatus{};

        struct PendingMelee {
            CBaseHandle weapon{};
            int command = 0;
            double ready = 0.;
        } pendingMelee;

        bool Behind(C_CSPlayerPawn* local, C_CSPlayerPawn* target) {
            auto delta = target->m_pGameSceneNode()->m_vecAbsOrigin() - local->m_pGameSceneNode()->m_vecAbsOrigin();
            delta.z = 0;
            delta = delta.Normalize();
            const auto eyeOffset = SchemaFinder::Get(hash_32_fnv1a_const("C_CSPlayerPawn->m_angEyeAngles"));
            const auto rootOffset = SchemaFinder::Get(hash_32_fnv1a_const("CGameSceneNode->m_angAbsRotation"));
            QAngle_t angle{};
            Vector_t forward;
            // Native Swing accepts either the rendered body or eye direction. Do not
            // normalize away eye pitch: that would invent backstabs at steep angles.
            if (rootOffset && Mem::ReadField(target->m_pGameSceneNode(), rootOffset, angle) && angle.IsValid()) {
                angle.ToDirections(&forward);
                if (Legitbot::Dot(delta, forward) > .475f)
                    return true;
            }
            if (eyeOffset && Mem::ReadField(target, eyeOffset, angle) && angle.IsValid()) {
                angle.ToDirections(&forward);
                if (Legitbot::Dot(delta, forward) > .475f)
                    return true;
            }
            return false;
        }

        std::array<float, 2>
        KnifeReach(C_CSPlayerPawn* local, C_CSPlayerPawn* target, const Vector_t& eye, const Vector_t& point) {
            std::array<float, 2> times{Knife::never, Knife::never};
            const auto direction = (point - eye).Normalize();
            Trace::CGameTrace slash{}, stab{};
            if (Trace::TraceKnifeAttack(eye, direction, false, local, slash) &&
                Trace::HitsTarget(slash.hit_entity(), target))
                times[0] = 0.f;
            if (Trace::TraceKnifeAttack(eye, direction, true, local, stab) &&
                Trace::HitsTarget(stab.hit_entity(), target))
                times[1] = 0.f;
            if (times[0] != 0.f || times[1] == 0.f)
                return times;
            const auto velocity = local->m_vecVelocity() - target->m_vecVelocity();
            const float closing = Legitbot::Dot(velocity, direction);
            if (closing <= 0.f)
                return times;
            // Hull traces report the swept center, not the frontmost contact. The
            // attack ranges differ by 16 units; cover that gap before testing a stab.
            const float wait = 18.f / closing;
            if (wait >= Knife::cycle[0])
                return times;
            const auto future = eye + velocity * wait;
            Trace::CGameTrace path{};
            if (Trace::TraceLine(eye, future, local, path, Trace::kMaskShot, 3, 15) && !Trace::DidHit(path) &&
                Reach(local, target, future, point, 50.f, true, true)) {
                float low = 0.f, high = wait;
                for (int i = 0; i < 4; ++i) {
                    const float middle = (low + high) * .5f;
                    if (Reach(local, target, eye + velocity * middle, point, 50.f, true, true))
                        high = middle;
                    else
                        low = middle;
                }
                times[1] = high;
            }
            return times;
        }

    } // namespace

    static Decision SubmitMeleeImpl(CUserCmd* cmd,
                                    C_CSPlayerPawn* local,
                                    const Settings& c,
                                    const QAngle_t& view,
                                    bool manual,
                                    Submission& result) {
        result = {};
        auto* weapon = local->GetActiveWeapon();
        auto* data = weapon ? weapon->Data() : nullptr;
        if (!data || !I::GameEntity || !I::GameEntity->Instance)
            return Decision::WeaponNotReady;
        const bool knife = data->m_WeaponType() == 0;
        if ((knife && !Config::melee.knife) || (!knife && !Config::melee.zeus))
            return Decision::Idle;
        if (!knife && (weapon->m_iClip1() <= 0 || weapon->m_bInReload()))
            return Decision::WeaponNotReady;
        auto* controller = I::GameEntity->Instance->Get<CCSPlayerController>(local->m_hController());
        if (!controller)
            return Decision::WeaponNotReady;
        const int index = cmd->csgoUserCmd.inputHistoryField.nCurrentSize - 1;
        auto* history = cmd->GetInputHistoryEntry(index);
        if (!history || !history->pViewAngles)
            return Decision::NoHistory;
        Legitbot::SeedShot timing{};
        const bool timed = Legitbot::ReadSeedShotAt(cmd, local, weapon, index, false, timing);
        const double clock = timed ? double(timing.tick) + timing.fraction : double(controller->m_nTickBase());
        const Vector_t eye = timed ? timing.eye : Bones::GetEyePos(local);
        Candidate best{};
        C_CSPlayerPawn* bestPawn = nullptr;
        bool secondary = false;
        float score = -(std::numeric_limits<float>::max)();
        Knife::Plan bestPlan{};
        const std::array<double, 2> next{
            double(weapon->m_nNextPrimaryAttackTick()) + weapon->m_flNextPrimaryAttackTickRatio(),
            double(weapon->m_nNextSecondaryAttackTick()) + weapon->m_flNextSecondaryAttackTickRatio()};
        const auto weaponHandle = reinterpret_cast<CEntityInstance*>(weapon)->handle();
        if (!manual && pendingMelee.weapon == weaponHandle) {
            const auto offset =
                SchemaFinder::Get(hash_32_fnv1a_const("CPlayer_MovementServices->m_nLastCommandNumberProcessed"));
            int processed = 0;
            if (next[0] <= pendingMelee.ready + 1e-6 &&
                (!offset || !Mem::ReadField(local->m_pMovementServices(), offset, processed) ||
                 processed <= pendingMelee.command))
                return Decision::WeaponNotReady;
            pendingMelee = {};
        }
        const std::array<float, 2> ready{float((std::max)(0., next[0] - clock) * Knife::tick),
                                         float((std::max)(0., next[1] - clock) * Knife::tick)};
        const auto firstOffset = knife ? SchemaFinder::Get(hash_32_fnv1a_const("C_Knife->m_bFirstAttack")) : 0;
        bool firstFlag = false;
        const bool first = firstOffset && Mem::ReadField(weapon, firstOffset, firstFlag) && firstFlag;
        float firstAt = first ? -Knife::never : float((weapon->m_nNextPrimaryAttackTick() + 26. - clock) * Knife::tick);
        if (knifeTap.firstCommand)
            firstAt = float((knifeTap.firstReset - clock) * Knife::tick);
        float chargeAt = Knife::never, chargeAge = 0.f;
        if (knife && Config::melee.knife_dt && !manual && !knifeTap.firstCommand && timed) {
            int primary = 0, secondaryTick = 0;
            if (Legitbot::ChargedShotTime(local, weapon, primary) &&
                Legitbot::ChargedShotTime(local, weapon, secondaryTick, true)) {
                const double elapsed = (clock - (std::max)(next[0], next[1])) * Knife::tick;
                chargeAt = float((std::max)(0., double(Knife::cycle[1]) - elapsed));
                chargeAge = float(elapsed);
                Legitbot::SeedShot preview{};
                if (chargeAt == 0.f && !Legitbot::ReadSeedShotAt(cmd, local, weapon, index, true, preview, true))
                    chargeAt = Knife::never;
                knifeStatus = {true,
                               !std::isfinite(chargeAt) ? DoubleTapState::Unavailable
                               : chargeAt == 0.f        ? DoubleTapState::Ready
                                                        : DoubleTapState::Charging,
                               std::isfinite(chargeAt) ? float(std::clamp(elapsed / Knife::cycle[1], 0., 1.)) : 0.f};
            }
        }
        int indices[SdkPrioA::kMaxTrackedControllers]{};
        const int count = SdkPrioA::CopyControllerIndices(indices, SdkPrioA::kMaxTrackedControllers);
        for (int i = 0; i < count; ++i) {
            auto* ctrl = I::GameEntity->Instance->Get<CCSPlayerController>(indices[i]);
            if (!ctrl || !ctrl->m_bPawnIsAlive())
                continue;
            const auto handle = ctrl->m_hPlayerPawn();
            auto* pawn = I::GameEntity->Instance->Get<C_CSPlayerPawn>(handle);
            if (!pawn || pawn == local || pawn->m_iHealth() <= 0 || pawn->m_lifeState() != 0 ||
                pawn->m_bGunGameImmunity() ||
                (GameMode::WantTeamCheck(true) && pawn->m_iTeamNum() == local->m_iTeamNum()))
                continue;
            auto* node = pawn->m_pGameSceneNode();
            if (!node || node->m_bDormant())
                continue;
            const auto point = node->m_vecAbsOrigin() + Vector_t{0, 0, 48};
            const float distance = (point - eye).Length();
            const float primaryRange = knife ? 66.f : data->m_flRange();
            if (distance > primaryRange + 32.f || !InAimCone(point - eye, view, c.fov))
                continue;
            if (knife) {
                auto reach = KnifeReach(local, pawn, eye, point);
                if (Config::melee.knife_attack == 1 || (manual && !(cmd->nButtons.nValue & IN_SECOND_ATTACK)))
                    reach[1] = Knife::never;
                if (Config::melee.knife_attack == 2 || (manual && !(cmd->nButtons.nValue & IN_ATTACK)))
                    reach[0] = Knife::never;
                int health = pawn->m_iHealth(), armor = pawn->m_ArmorValue();
                if (knifeTap.firstCommand && handle == knifeTap.target) {
                    health = (std::min)(health, knifeTap.health - knifeTap.damage);
                    armor = (std::min)(armor, knifeTap.armor);
                    if (health <= 0)
                        continue;
                }
                const auto plan =
                    Knife::PlanAttack(health, armor, Behind(local, pawn), ready, reach, firstAt, chargeAt, chargeAge);
                if (plan.attack < 0)
                    continue;
                if (!best || plan.kill < bestPlan.kill ||
                    (plan.kill == bestPlan.kill &&
                     (plan.hits < bestPlan.hits || (plan.hits == bestPlan.hits && distance < best.distance)))) {
                    bestPlan = plan;
                    secondary = plan.attack == 1;
                    bestPawn = pawn;
                    best.handle = handle;
                    best.point = point;
                    best.group = 3;
                    best.health = pawn->m_iHealth();
                    best.damage = float(plan.damage);
                    best.distance = distance;
                    best.shootable = true;
                }
            } else {
                if (clock < next[0] || !Reach(local, pawn, eye, point, primaryRange, false, false))
                    continue;
                const float damage = Legitbot::ShotDamage(eye, point, 3, local, pawn, weapon);
                if (damage < 1.f || (Config::melee.zeus_lethal && damage < pawn->m_iHealth()))
                    continue;
                const float cycle = (std::max)(.01f, data->m_flCycleTimePrimary());
                const float value = (damage >= pawn->m_iHealth() ? 100000.f : 0.f) + damage / cycle - distance * .01f;
                if (value > score) {
                    score = value;
                    bestPawn = pawn;
                    best.handle = handle;
                    best.point = point;
                    best.group = 3;
                    best.health = pawn->m_iHealth();
                    best.damage = damage;
                    best.distance = distance;
                    best.shootable = true;
                }
            }
        }
        if (!best)
            return Decision::NoTarget;
        if (knife && bestPlan.start > 1e-6f)
            return Decision::Preparing;
        if (knife && bestPlan.charged) {
            Legitbot::SeedShot charged{};
            if (!Legitbot::ChargedShotTime(local, weapon, knifeTap.historyTick, secondary) ||
                !Legitbot::ReadSeedShotAt(cmd, local, weapon, index, secondary, charged, true)) {
                knifeStatus = {true, DoubleTapState::Unavailable, 0.f};
                return Decision::NoSeed;
            }
            if (!Reach(local, bestPawn, charged.eye, best.point, secondary ? 50.f : 66.f, true, secondary))
                return Decision::TargetChanged;
            knifeTap.secondary = secondary;
        }
        const auto display = AimAngle(eye, best.point);
        auto angle = display;
        if (!knife && timed)
            angle = angle - timing.punch;
        const bool hidden = c.silent && c.hide_shots;
        QAngle_t lagCompReference{};
        if (hidden && !ReadLagCompReference(local, bestPawn, lagCompReference))
            return Decision::TargetChanged;
        angle.Normalize();
        if (!CommitAngle(cmd,
                         index,
                         angle,
                         c.silent,
                         hidden,
                         secondary,
                         display,
                         lagCompReference,
                         local,
                         MovementGenerated(cmd)))
            return Decision::NoHistory;
        if (!manual) {
            InputInject::SetHeld(cmd, secondary ? IN_ATTACK : IN_SECOND_ATTACK, false);
            InputInject::SetEdge(cmd, secondary ? IN_SECOND_ATTACK : IN_ATTACK, true);
        }
        result = {true, !manual, secondary, hidden, cmd->nCommandNumber, index, angle, best, lagCompReference};
        if (!manual)
            pendingMelee = {weaponHandle, cmd->nCommandNumber, next[0]};
        if (knife && bestPlan.charged) {
            const auto hit =
                Knife::Damage(secondary, Behind(local, bestPawn), 0.f >= firstAt, bestPawn->m_ArmorValue());
            knifeTap.firstCommand = cmd->nCommandNumber;
            knifeTap.untilCommand = cmd->nCommandNumber + int(std::ceil(Knife::cycle[1] / Knife::tick));
            knifeTap.angle = angle;
            knifeTap.target = best.handle;
            knifeTap.health = best.health;
            knifeTap.damage = bestPlan.damage;
            knifeTap.armor = hit.armor;
            knifeTap.firstReset = next[secondary ? 1 : 0] + Knife::cycle[secondary ? 1 : 0] / Knife::tick + 26.;
            knifeStatus = {true, DoubleTapState::SecondShot, 0.f};
        } else if (knifeTap.firstCommand)
            knifeTap.firstCommand = 0;
        if (Active() && HitLog::Ready())
            HitLog::Record(cmd, local, weapon, best, eye, display, 100.f, true);
        return Decision::Fired;
    }

    void ResetMelee() {
        knifeTap = {};
        pendingMelee = {};
        knifeStatus = {};
        SetDoubleTapStatus(knifeStatus);
    }

    void ReplayMelee(CUserCmd* cmd) {
        if (!cmd || cmd->nCommandNumber != knifeTap.command || knifeTap.historyTick < 0)
            return;
        RewriteDoubleTapCommand(cmd, knifeTap.historyTick, knifeTap.secondary, &knifeTap.angle);
        if (!knifeTap.historyTick)
            RewriteDoubleTapCommand(cmd, 0, !knifeTap.secondary);
    }

    Decision SubmitMelee(CUserCmd* cmd,
                         C_CSPlayerPawn* local,
                         const Settings& c,
                         const QAngle_t& view,
                         bool manual,
                         Submission& result) {
        auto* weapon = local->GetActiveWeapon();
        auto* data = weapon ? weapon->Data() : nullptr;
        const bool enabled =
            data && data->m_WeaponType() == 0 && Config::melee.knife && Config::melee.knife_dt && !manual;
        const auto handle = weapon ? reinterpret_cast<CEntityInstance*>(weapon)->handle() : CBaseHandle{};
        if (!enabled || knifeTap.weapon != handle || cmd->nCommandNumber < knifeTap.command)
            knifeTap = {};
        knifeStatus = {enabled, DoubleTapState::Unavailable, 0.f};
        if (enabled) {
            knifeTap.weapon = handle;
            knifeTap.command = cmd->nCommandNumber;
            knifeTap.historyTick = 0;
            if (knifeTap.firstCommand && cmd->nCommandNumber > knifeTap.untilCommand)
                knifeTap.firstCommand = 0;
            if (knifeTap.firstCommand) {
                knifeStatus = {true, DoubleTapState::SecondShot, 0.f};
                if (cmd->nCommandNumber == knifeTap.firstCommand + 1) {
                    result = {};
                    ReplayMelee(cmd);
                    SetDoubleTapStatus(knifeStatus);
                    return Decision::Preparing;
                }
            }
        }
        const auto decision = SubmitMeleeImpl(cmd, local, c, view, manual, result);
        if (enabled) {
            if (!result.fired)
                knifeTap.historyTick = 0;
            else if (knifeTap.firstCommand != cmd->nCommandNumber)
                knifeTap.historyTick = -1;
            ReplayMelee(cmd);
        }
        SetDoubleTapStatus(knifeStatus);
        return decision;
    }

    void RunStandaloneMelee(CUserCmd* cmd, C_CSPlayerPawn* local) {
        static int command = 0;
        static CBaseHandle held{};
        static Submission shot{};
        if (Active()) {
            command = 0;
            shot = {};
            return;
        }
        if (!cmd || !local || (!Config::melee.knife && !Config::melee.zeus) || SdkPrioA::ShouldSoftDisableCombat()) {
            command = 0;
            shot = {};
            ResetMelee();
            return;
        }
        auto* weapon = local->GetActiveWeapon();
        if (!IsMeleeWeapon(weapon)) {
            command = 0;
            shot = {};
            ResetMelee();
            return;
        }
        const auto handle = reinterpret_cast<CEntityInstance*>(weapon)->handle();
        if (command == cmd->nCommandNumber && handle == held) {
            if (shot.fired)
                ReplayAngle(cmd, shot.index, shot.angle, shot.secondary, shot.automatic);
            ReplayMelee(cmd);
            return;
        }
        command = cmd->nCommandNumber;
        held = handle;
        shot = {};
        QAngle_t view{};
        if (!Input::ReadViewAngles(view) || !view.IsValid())
            return;
        Settings settings{};
        settings.silent = true;
        settings.hide_shots = true;
        settings.fov = 180;
        SubmitMelee(cmd, local, settings, view, (cmd->nButtons.nValue & (IN_ATTACK | IN_SECOND_ATTACK)) != 0, shot);
    }
} // namespace Rage
