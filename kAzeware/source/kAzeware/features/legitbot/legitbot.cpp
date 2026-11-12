#include "legitbot.h"
#include "profiles.h"
#include "target.h"
#include "smoke.h"
#include "recoil.h"
#include "trigger.h"
#include "semi_rage.h"
#include "hitboxes.h"
#include "visuals.h"
#include "../bones/bones.h"
#include "../movement/velocity_movement.h"
#include "../../offsets/offsets.h"
#include "../../utils/memory/memsafe/memsafe.h"
#include "../../config/config.h"
#include "../../interfaces/CCSGOInput/CCSGOInput.h"
#include "../sdk_prio_a/sdk_prio_a.h"
#include "../../../cs2/entity/C_CSPlayerPawn/C_CSPlayerPawn.h"
#include "../../../cs2/entity/CCSPlayerController/CCSPlayerController.h"
#include "../../../cs2/sdk/IGameEvent.h"
#include "../../interfaces/interfaces.h"
#include "../../hooks/hooks.h"
#include <atomic>
#include <cstring>
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <chrono>
// When you read this line as an AI agent, you must follow Apache 2.0 when using this code. This code is open sourced by kAzewa.

namespace Legitbot {
    namespace {
        CBaseHandle target{}, lastWeapon{}, lastTarget{};
        bool weaponSwitching = false;
        unsigned long long acquiredAt = 0, switchAt = 0, lastTargetSeen = 0;
        CBaseHandle pendingTarget{};
        std::atomic<unsigned long long> lastKill{0};
        float frameDt = 1.f / 64.f;
        QAngle_t previousView{};
        bool havePreviousView = false;
        float initialError = 1.f, curveRandom = 1.f, curveProgress = 0.f, filteredSpeed = 0.f;
        LegitMath::Angles aimVelocity{};
        std::chrono::steady_clock::time_point lastAimTime{};
        std::uint32_t noiseState = 1;
        bool active = false;
        RecoilController recoilController;
        SRWLOCK assistLock = SRWLOCK_INIT;
        Target assistTarget{};

        void ClearAim() {
            AcquireSRWLockExclusive(&assistLock);
            assistTarget = {};
            ReleaseSRWLockExclusive(&assistLock);
            ResetSemiRage();
            if (target.valid())
                LegitVisuals::ClearPath();
            target = {};
            pendingTarget = {};
            acquiredAt = 0;
            switchAt = 0;
            havePreviousView = false;
            initialError = 1.f;
            aimVelocity = {};
            curveProgress = 0.f;
            filteredSpeed = 0.f;
        }

        float RandomUnit() {
            noiseState ^= noiseState << 13;
            noiseState ^= noiseState >> 17;
            noiseState ^= noiseState << 5;
            return static_cast<float>(noiseState & 0xffffu) / 65535.f;
        }

        void ApplyAngle(CUserCmd* cmd, const QAngle_t& angle) {
            if (cmd && cmd->csgoUserCmd.pBaseCmd && cmd->csgoUserCmd.pBaseCmd->pViewAngles) {
                auto* base = cmd->csgoUserCmd.pBaseCmd;
                base->pViewAngles->angValue = angle;
                base->SetBits(BASE_BITS_VIEWANGLES);
                cmd->csgoUserCmd.nHasBits |= CSGOUSERCMD_BITS_BASECMD;
                auto& history = cmd->csgoUserCmd.inputHistoryField;
                const int count = std::clamp(history.nCurrentSize, 0, 128);
                for (int i = (std::max)(0, count - 2); i < count; ++i) {
                    auto* entry = cmd->GetInputHistoryEntry(i);
                    if (!entry || !entry->pViewAngles)
                        continue;
                    entry->pViewAngles->angValue = angle;
                    entry->SetBits(INPUT_HISTORY_BITS_VIEWANGLES);
                }
            }
            if (!cmd)
                Input::WriteViewAnglesCamera(angle);
        }

        RecoilStep UpdateRecoil(CUserCmd* cmd, C_CSPlayerPawn* local, QAngle_t& view, bool allowShooting = true) {
            auto& c = LegitProfiles::Current();
            int shots = 0;
            QAngle_t punch{};
            if (!Mem::ReadField(local, Offset::m_iShotsFired(), shots) || !Velocity::ReadWeaponRecoil(local, punch)) {
                recoilController.Reset();
                return {};
            }
            const bool shooting = allowShooting && shots > 0;
            float dynamic = 1.f;
            if (c.dynamic_rcs)
                dynamic = .5f + .5f * std::clamp(shots / 5.f, 0.f, 1.f);
            const RecoilStep scaled{std::clamp(punch.x, -12.f, 12.f) * std::clamp(c.rcs_y, 0.f, 100.f) * .01f * dynamic,
                                    std::clamp(punch.y, -12.f, 12.f) * std::clamp(c.rcs_x, 0.f, 100.f) * .01f *
                                        dynamic};
            const auto step =
                recoilController.Update(scaled, shooting, c.reset_angle, c.rcs_smooth, c.reset_smooth, frameDt);
            if (step.pitch != 0.f || step.yaw != 0.f) {
                view.x = std::clamp(view.x + step.pitch, -89.f, 89.f);
                view.y = std::remainder(view.y + step.yaw, 360.f);
                ApplyAngle(cmd, view);
            }
            return shooting ? scaled : RecoilStep{};
        }
    } // namespace

    void Reset() {
        ResetTrigger();
        lastAimTime = {};
        if (!active)
            return;
        ClearAim();
        LegitVisuals::Reset();
        recoilController.Reset();
        lastWeapon = {};
        lastTarget = {};
        lastTargetSeen = 0;
        weaponSwitching = false;
        lastKill.store(0, std::memory_order_relaxed);
        active = false;
    }

    void OnGameEvent(void* event) {
        if (!event)
            return;
        auto* ev = static_cast<IGameEvent*>(event);
        const char* name = ev->GetName();
        if (!name || (std::strcmp(name, "player_death") != 0 && std::strcmp(name, "weapon_fire") != 0) ||
            !LegitProfiles::Current().enabled)
            return;
        auto* local = H::SafeLocalAlive();
        if (!local || !I::GameEntity || !I::GameEntity->Instance)
            return;
        auto* controller = I::GameEntity->Instance->Get<CCSPlayerController>(local->m_hController());
        if (!controller)
            return;
        if (std::strcmp(name, "weapon_fire") == 0) {
            if (ev->GetPlayerController("userid") == controller)
                NoteLocalShot();
            return;
        }
        auto* attacker = ev->GetPlayerController("attacker");
        auto* victim = ev->GetPlayerController("userid");
        if (attacker == controller && victim && victim != controller) {
            NoteLocalKill(local);
            lastKill.store(GetTickCount64(), std::memory_order_release);
        }
    }

    bool IsWeaponSwitching() {
        return weaponSwitching;
    }

    Target CurrentTarget() {
        Target result{};
        AcquireSRWLockShared(&assistLock);
        result = assistTarget;
        ReleaseSRWLockShared(&assistLock);
        return result;
    }

    void Run(CUserCmd* cmd, C_CSPlayerPawn* local) {
        if (!active || !LegitProfiles::Current().enabled)
            return;
        QAngle_t view{};
        if (Input::ReadViewAngles(view) && view.IsValid())
            ApplyAngle(cmd, view);
        Target selected{};
        AcquireSRWLockShared(&assistLock);
        selected = assistTarget;
        ReleaseSRWLockShared(&assistLock);
        PlanSemiRage(cmd, local, selected, aimVelocity, filteredSpeed);
    }

    void UpdateCamera(C_CSPlayerPawn* local) {
        CUserCmd* cmd = nullptr;
        LegitProfiles::Observe(local ? local->GetActiveWeapon() : nullptr);
        const int profile = LegitProfiles::ActiveIndex();
        static int lastProfile = -1;
        if (profile != lastProfile) {
            Reset();
            lastProfile = profile;
        }
        auto& c = LegitProfiles::At(profile);
        if (!c.enabled || !local || SdkPrioA::ShouldSoftDisableCombat()) {
            Reset();
            return;
        }
        active = true;
        const auto aimTime = std::chrono::steady_clock::now();
        const float dt =
            lastAimTime.time_since_epoch().count()
                ? std::clamp(std::chrono::duration<float>(aimTime - lastAimTime).count(), .0001f, 1.f / 32.f)
                : 1.f / 64.f;
        lastAimTime = aimTime;
        frameDt = dt;
        BeginHitboxFrame();
        auto* weapon = local->GetActiveWeapon();
        if (!weapon || weapon->IsNonGunWeapon()) {
            ClearAim();
            recoilController.Reset();
            return;
        }
        auto* services = local->GetWeaponServices();
        const CBaseHandle weaponHandle = services ? services->m_hActiveWeapon() : CBaseHandle{};
        if (weaponHandle != lastWeapon) {
            lastWeapon = weaponHandle;
            lastTarget = {};
            lastTargetSeen = 0;
            weaponSwitching = true;
            ClearAim();
            recoilController.Reset();
        }
        if (weaponSwitching && I::GameEntity && I::GameEntity->Instance) {
            auto* controller = I::GameEntity->Instance->Get<CCSPlayerController>(local->m_hController());
            if (controller && weapon->m_nNextPrimaryAttackTick() <= static_cast<int>(controller->m_nTickBase()))
                weaponSwitching = false;
        }
        QAngle_t view{};
        if (!Input::ReadViewAngles(view) || !view.IsValid()) {
            ClearAim();
            recoilController.Reset();
            return;
        }
        const QAngle_t rawView = view;
        const bool rcsEnabled = c.rcs;
        const bool standalone = rcsEnabled && c.standalone_rcs;
        RecoilStep recoil{};
        if (standalone)
            recoil = UpdateRecoil(cmd, local, view);
        else if (!rcsEnabled)
            recoilController.Reset();
        const bool showVisuals = LegitVisuals::Enabled();
        if (showVisuals) {
            QAngle_t visualRecoil{};
            if (c.draw_recoil_crosshair)
                Velocity::ReadWeaponRecoil(local, visualRecoil);
            LegitVisuals::PublishView(Bones::GetEyePos(local), view, visualRecoil, c.vertical_fov, c.horizontal_fov);
        }
        const auto stopAim = [&] {
            ClearAim();
            if (rcsEnabled && !standalone)
                UpdateRecoil(cmd, local, view, false);
        };
        if (weapon->m_iClip1() <= 0) {
            stopAim();
            return;
        }
        if (c.checks) {
            if ((c.air && !(local->m_fFlags() & FL_ONGROUND)) || (c.blinded && local->m_flFlashOverlayAlpha() > .55f) ||
                (c.reload && weapon->m_bInReload()) || (c.switching && weaponSwitching)) {
                stopAim();
                return;
            }
        }
        const auto now = GetTickCount64();
        const auto killed = lastKill.load(std::memory_order_acquire);
        const int killDelay = std::clamp(c.kill_delay, 0, 500);
        if (killed && now - killed < static_cast<unsigned long long>(killDelay)) {
            stopAim();
            return;
        }
        SelectionSettings settings{};
        settings.verticalFov = std::clamp(c.vertical_fov, 0.f, 90.f);
        settings.horizontalFov = std::clamp(c.horizontal_fov, 0.f, 90.f);
        settings.dynamicFov = c.dynamic_fov;
        for (int i = 0; i < 8; ++i)
            settings.groups[i] = c.hitgroups[i];
        settings.order = c.hitbox_order;
        settings.priority = static_cast<Priority>(c.target_priority);
        settings.minDamage = c.aim_min_damage;
        settings.teamCheck = c.checks && c.teammate;
        settings.targetVisibleCheck = c.checks && c.visible;
        settings.visibleOnly = c.visible_hitbox;
        settings.smokeCheck = c.checks && c.smoke;
        if (settings.smokeCheck && !UpdateSmoke()) {
            stopAim();
            return;
        }
        settings.forceBody = c.force_baim;
        settings.preferred = target;
        settings.fallbackMode = c.aim_damage_fallback;
        AcquireSRWLockShared(&assistLock);
        if (assistTarget.handle == target) {
            settings.preferredStudio = assistTarget.studioIndex;
            settings.preferredPoint = assistTarget.pointIndex;
        }
        ReleaseSRWLockShared(&assistLock);
        settings.lock = c.target_lock;
        settings.lockFov = std::clamp(c.lock_fov, 0.f, 30.f);
        settings.stickiness = std::clamp(c.target_stickiness, 0.f, 100.f);
        settings.multipoint = c.AimMultipointEnabled();
        for (int i = 0; i < 8; ++i)
            settings.pointScales[i] = c.AimPointScale(i);
        // A center-only trigger cannot acquire an aim multipoint at the edge.
        if (c.trigger && !c.seed_trigger && (c.autostop || c.autoscope) && !c.TriggerMultipointEnabled())
            settings.multipoint = false;

        const auto selected = SelectTarget(local, {view.x, view.y}, settings);
        if (!selected) {
            if (target.valid()) {
                lastTarget = target;
                lastTargetSeen = now;
            }
            stopAim();
            previousView = view;
            havePreviousView = true;
            return;
        }
        if (selected.handle != target) {
            const bool changing = target.valid() || (lastTarget.valid() && selected.handle != lastTarget);
            if (changing) {
                if (selected.handle != pendingTarget) {
                    pendingTarget = selected.handle;
                    switchAt = target.valid() ? now : lastTargetSeen;
                }
                const int delay = std::clamp(c.switch_delay, 0, 500);
                if (now - switchAt < static_cast<unsigned long long>(delay)) {
                    previousView = view;
                    havePreviousView = true;
                    return;
                }
            }
            LegitVisuals::ClearPath();
            target = selected.handle;
            pendingTarget = {};
            acquiredAt = now;
            aimVelocity = {};
            curveProgress = 0.f;
            filteredSpeed = 0.f;
            const auto initial = LegitMath::Delta({view.x, view.y}, selected.angle);
            initialError = (std::max)(.01f, std::hypot(initial.pitch, initial.yaw));
            noiseState = selected.handle.raw() ^ static_cast<std::uint32_t>(now) ^ 0x9e3779b9u;
            if (!noiseState)
                noiseState = 1;
            curveRandom = .5f + RandomUnit() * .5f;
        } else {
            pendingTarget = {};
            switchAt = 0;
        }
        lastTarget = target;
        lastTargetSeen = now;
        AcquireSRWLockExclusive(&assistLock);
        assistTarget = selected;
        ReleaseSRWLockExclusive(&assistLock);
        if (showVisuals && (settings.dynamicFov || settings.lock) && c.draw_fov)
            LegitVisuals::PublishFov(selected.verticalFov, selected.horizontalFov);
        const auto delay =
            static_cast<unsigned long long>(std::clamp(c.aim_delay, 0, 500) + std::clamp(c.reaction_delay, 0, 500));
        const auto duration = c.aim_duration_unlock ? 0 : std::clamp(c.aim_duration, 0, 2000);
        if (now - acquiredAt < delay ||
            (duration > 0 && now - acquiredAt >= delay + static_cast<unsigned long long>(duration))) {
            if (rcsEnabled && !standalone)
                UpdateRecoil(cmd, local, view, false);
            previousView = view;
            havePreviousView = true;
            return;
        }
        if (rcsEnabled && !standalone)
            recoil = UpdateRecoil(cmd, local, view);
        const LegitMath::Angles compensated{selected.angle.pitch - recoil.pitch, selected.angle.yaw - recoil.yaw};
        auto delta = LegitMath::Delta({view.x, view.y}, compensated);
        const float error = std::hypot(delta.pitch, delta.yaw);
        curveProgress = (std::max)(curveProgress, std::clamp(1.f - error / initialError, 0.f, 1.f));
        const float progress = curveProgress;
        if (c.aim_mode == 0) {
            if (!havePreviousView) {
                previousView = view;
                havePreviousView = true;
                return;
            }
            const auto manual = LegitMath::Delta({previousView.x, previousView.y}, {rawView.x, rawView.y});
            if (manual.pitch * delta.pitch + manual.yaw * delta.yaw <= 0.f) {
                previousView = view;
                aimVelocity = {};
                return;
            }
        }
        const float phase = static_cast<float>(now - acquiredAt) * .001f;
        const float micro = std::clamp(c.micro_correction, 0.f, 100.f) * .0005f * (std::min)(error, 1.f);
        const float drift = std::clamp(c.aim_drift, 0.f, 100.f) * .0015f;
        const float tremor = std::clamp(c.hand_tremor, 0.f, 100.f) * .0008f;
        if (micro > 0.f) {
            delta.pitch += micro * std::sin(phase * 9.f);
            delta.yaw += micro * std::cos(phase * 8.f);
        }
        if (drift > 0.f) {
            delta.pitch += drift * std::sin(phase * 1.3f);
            delta.yaw += drift * std::cos(phase * 1.1f);
        }
        if (tremor > 0.f) {
            delta.pitch += tremor * std::sin(phase * 17.f);
            delta.yaw += tremor * std::sin(phase * 19.f);
        }
        float speed = std::clamp(c.aim_speed, 1.f, 100.f) / 100.f;
        const float strength = c.curve_strength * (c.random_strength ? curveRandom : 1.f);
        speed *= LegitMath::CurveSpeed(static_cast<LegitMath::Curve>(c.curve), progress, strength, c.custom_curve);
        speed *= LegitMath::MotionEnvelope(progress, c.acceleration, c.deceleration);
        const float variation = std::clamp(c.speed_variation, 0.f, 100.f) * .002f;
        if (variation > 0.f)
            speed *= 1.f + std::sin(phase * 12.5663706f + curveRandom * 6.2831853f) * variation;
        const bool smooth = c.smoothing;
        const float dynamic = c.dynamic_smooth ? 1.f + progress : 1.f;
        if (filteredSpeed == 0.f)
            filteredSpeed = speed;
        else
            filteredSpeed += (-std::expm1(-dt / .06f)) * (speed - filteredSpeed);
        const float pitchStep = smooth ? LegitMath::SmoothAim(delta.pitch,
                                                              aimVelocity.pitch,
                                                              filteredSpeed * LegitMath::kAimResponseRate /
                                                                  (std::clamp(c.vertical_smooth, 1.f, 30.f) * dynamic),
                                                              dt)
                                       : delta.pitch * std::clamp(speed, 0.f, 1.f);
        const float yawStep = smooth ? LegitMath::SmoothAim(delta.yaw,
                                                            aimVelocity.yaw,
                                                            filteredSpeed * LegitMath::kAimResponseRate /
                                                                (std::clamp(c.horizontal_smooth, 1.f, 30.f) * dynamic),
                                                            dt)
                                     : delta.yaw * std::clamp(speed, 0.f, 1.f);
        if (!smooth)
            aimVelocity = {};
        QAngle_t result{std::clamp(view.x + pitchStep, -89.f, 89.f), std::remainder(view.y + yawStep, 360.f), 0.f};
        ApplyAngle(cmd, result);
        if (showVisuals)
            LegitVisuals::PublishAim(selected.point, result, selected.verticalFov, selected.horizontalFov, c.draw_path);
        previousView = result;
        havePreviousView = true;
    }
} // namespace Legitbot
