#include "rage.h"
#include "visualise.h"
#include "profiles.h"
#include "target.h"
#include "motion.h"
#include "shots.h"
#include "angles.h"
#include "anti_aim.h"
#include "history.h"
#include "melee.h"
#include "../movement/peek_assist.h"
#include "../legitbot/profiles.h"
#include "../legitbot/legitbot.h"
#include "../sdk_prio_a/sdk_prio_a.h"
#include "../../config/config.h"
#include "../../hooks/hooks.h"
#include "../../interfaces/interfaces.h"
#include "../../interfaces/CCSGOInput/CCSGOInput.h"
#include "../../interfaces/CUserCmd/CUserCmd.h"
#include "../../../cs2/entity/CCSPlayerController/CCSPlayerController.h"
#include "../../../cs2/sdk/IGameEvent.h"
#include <Windows.h>
#include <atomic>
#include <cstring>

namespace Rage {
    namespace {
        int lastPrepared = 0, lastExecuted = 0, lastProfile = -1;
        CBaseHandle retained{}, heldWeapon{}, aaTarget{};
        QAngle_t realView{};
        bool manualAttack = false;
        bool movementGenerated = false;
        Submission submitted{};
        Candidate duckCandidate{};
        std::atomic<bool> clearRequested{false};

    } // namespace

    bool Active() {
        if (Config::loading.load(std::memory_order_acquire))
            return false;
        std::lock_guard guard(ProfileMutex());
        return !Config::loading.load(std::memory_order_acquire) && Profile(0).enabled;
    }

    bool HasPrepared(CUserCmd* cmd) {
        return cmd && cmd->nCommandNumber > 0 && cmd->nCommandNumber == lastPrepared;
    }

    void Reset() {
        Visualise::Reset();
        if (lastPrepared || lastExecuted) {
            ResetMelee();
            ResetShots();
        }
        lastPrepared = lastExecuted = 0;
        retained = {};
        heldWeapon = {};
        aaTarget = {};
        submitted = {};
        duckCandidate.handle = {};
        manualAttack = false;
        movementGenerated = false;
        ResetMotion();
        ResetAngles();
        ResetHistory();
        ResetTargetScan();
    }

    const Candidate* PreparedTarget(CUserCmd* cmd) {
        return HasPrepared(cmd) && duckCandidate ? &duckCandidate : nullptr;
    }

    bool IsPreparedTarget(CUserCmd* cmd, std::uint32_t handle) {
        return HasPrepared(cmd) && aaTarget.valid() && aaTarget.raw() == handle;
    }

    bool MovementGenerated(CUserCmd* cmd) {
        return HasPrepared(cmd) && movementGenerated;
    }

    // 命令准备阶段：读取实际视角、捕获目标姿态，再为移动辅助提供候选目标。
    void Prepare(CUserCmd* cmd, C_CSPlayerPawn* local) {
        if (!cmd || !local || !Active() || SdkPrioA::ShouldSoftDisableCombat()) {
            Reset();
            return;
        }
        LegitProfiles::Observe(local->GetActiveWeapon());
        const int profile = CurrentProfile();
        if (profile != lastProfile || clearRequested.exchange(false)) {
            Reset();
            lastProfile = profile;
        }
        auto* weapon = local->GetActiveWeapon();
        auto* service = local->GetWeaponServices();
        if (!weapon || !service) {
            Reset();
            return;
        }
        const auto handle = service->m_hActiveWeapon();
        if (heldWeapon != handle) {
            Reset();
            heldWeapon = handle;
        }
        auto* base = cmd->csgoUserCmd.pBaseCmd;
        if (!base || !base->pViewAngles)
            return;
        if (!Input::ReadViewAngles(realView) || !realView.IsValid())
            return;
        // Input camera and movement retain their real direction until a shot is
        // accepted. Anti-aim is applied after the movement generator finishes.
        base->pViewAngles->angValue = realView;
        base->pViewAngles->SetBits(7);
        base->SetBits(BASE_BITS_VIEWANGLES);
        manualAttack = (cmd->nButtons.nValue & (IN_ATTACK | IN_SECOND_ATTACK)) != 0;
        lastPrepared = cmd->nCommandNumber;
        const auto c = EffectiveShotSettings(Current());
        BeginTargetScan(cmd->nCommandNumber, local);
        CaptureHistory(cmd, local, c);
        const bool intent = c.auto_fire || manualAttack;
        Candidate target{};
        const bool directionTarget = c.anti_aim && c.yaw_basis == 1;
        if ((directionTarget || Config::movement.duck_peek) && !weapon->IsNonGunWeapon()) {
            target = SelectCandidate(local, weapon, Bones::GetEyePos(local), realView, c, retained, true);
        } else
            target.handle = retained;
        if (Config::movement.duck_peek)
            duckCandidate = target;
        else
            duckCandidate.handle = {};
        PrepareMotion(cmd, local, weapon, target, c, intent);
        aaTarget = target ? target.handle : retained;
    }

    // 移动生成完成后再应用 AA，并保留开火所需的位置历史。
    void AfterMovement(CUserCmd* cmd, C_CSPlayerPawn* local, bool generated) {
        if (Active() && cmd && cmd->nCommandNumber == lastPrepared) {
            movementGenerated = generated;
            ApplyAntiAim(cmd, local, Current(), realView, aaTarget, generated);
            PreserveShotPositionHistory(cmd, local, generated);
        }
    }

    // 同一命令重复执行时复用已提交结果；新命令进入对应武器的开火链路。
    void Execute(CUserCmd* cmd, C_CSPlayerPawn* local) {
        if (!cmd || !local || !Active() || SdkPrioA::ShouldSoftDisableCombat()) {
            Reset();
            return;
        }
        if (cmd->nCommandNumber && cmd->nCommandNumber == lastExecuted) {
            Replay(cmd);
            return;
        }
        if (lastPrepared != cmd->nCommandNumber) {
            if (!Input::ReadViewAngles(realView))
                return;
            manualAttack = (cmd->nButtons.nValue & (IN_ATTACK | IN_SECOND_ATTACK)) != 0;
        }
        lastExecuted = cmd->nCommandNumber;
        submitted = {};
        const auto c = Current();
        BeginTargetScan(cmd->nCommandNumber, local);
        CaptureHistory(cmd, local, c);
        const auto result = IsMeleeWeapon(local->GetActiveWeapon())
                                ? SubmitMelee(cmd, local, c, realView, manualAttack, submitted)
                                : SubmitGun(cmd, local, c, realView, manualAttack, retained, submitted);
        if (IsMeleeWeapon(local->GetActiveWeapon()))
            Visualise::Reset();
        if (submitted.fired)
            retained = submitted.target.handle;
        else if (result == Decision::NoTarget || result == Decision::TimedOut || result == Decision::TargetChanged)
            retained = {};
    }

    void Replay(CUserCmd* cmd) {
        if (Active() && cmd && submitted.fired && cmd->nCommandNumber == submitted.command) {
            auto c = EffectiveShotSettings(Current());
            c.double_tap_ready = submitted.doubleTapReady;
            if (CanApplyHistory(cmd,
                                submitted.index,
                                submitted.target,
                                c,
                                ForecastShotClock(cmd->csgoUserCmd.pBaseCmd->nClientTick)) &&
                (submitted.hidden
                     ? SetHiddenCommandAngle(
                           cmd, submitted.lagCompReference, H::SafeLocalAlive(), MovementGenerated(cmd))
                     : SetCommandAngle(cmd, submitted.angle, H::SafeLocalAlive(), MovementGenerated(cmd)))) {
                ReplayAngle(cmd, submitted.index, submitted.angle, submitted.secondary, submitted.automatic);
                ApplyHistory(cmd,
                             submitted.index,
                             submitted.target,
                             c,
                             ForecastShotClock(cmd->csgoUserCmd.pBaseCmd->nClientTick));
            }
        }
        ReplayDoubleTap(cmd);
        ReplayMelee(cmd);
    }

    void OnCamera(C_CSPlayerPawn* local) {
        LegitProfiles::Observe(local ? local->GetActiveWeapon() : nullptr);
        if (!local || !Active()) {
            ResetAngles();
            return;
        }
        ApplyShotCamera();
    }

    void OnEvent(void* raw) {
        PeekAssist::OnEvent(raw);
        if (!raw || !Active())
            return;
        auto* event = static_cast<IGameEvent*>(raw);
        const char* name = event->GetName();
        if (!name)
            return;
        auto* local = H::SafeLocalAlive();
        if (!local || !I::GameEntity || !I::GameEntity->Instance)
            return;
        auto* controller = I::GameEntity->Instance->Get<CCSPlayerController>(local->m_hController());
        if (!controller)
            return;
        if (!std::strcmp(name, "player_death") && event->GetPlayerController("userid") == controller)
            clearRequested.store(true);
    }
} // namespace Rage
