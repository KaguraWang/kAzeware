#include "peek_assist.h"
#include "../ragebot/rage.h"
#include "../ragebot/profiles.h"
#include "../ragebot/target.h"
#include "../ragebot/shots.h"
#include "../ragebot/motion.h"
#include "../legitbot/legitbot.h"
#include "../legitbot/target.h"
#include "../legitbot/profiles.h"
#include "../legitbot/damage.h"
#include "../legitbot/hitboxes.h"
#include "../legitbot/legit_math.h"
#include "../input_inject/input_inject.h"
#include "../trace/trace.h"
#include "../world/weather.h"
#include "../../config/config.h"
#include "../../hooks/hooks.h"
#include "../../interfaces/interfaces.h"
#include "../../interfaces/CUserCmd/CUserCmd.h"
#include "../../utils/schema/schema.h"
#include "../../../cs2/entity/C_CSPlayerPawn/C_CSPlayerPawn.h"
#include "../../../cs2/entity/C_CSWeaponBase/C_CSWeaponBase.h"
#include "../../../cs2/entity/CCSPlayerController/CCSPlayerController.h"
#include "../../../cs2/sdk/IGameEvent.h"
#include <Windows.h>
#include <atomic>
#include <limits>
#include <numbers>

namespace PeekAssist {
    namespace {
        constexpr float tick = 1.f / 64.f, rad = std::numbers::pi_v<float> / 180.f;
        std::atomic<bool> shotObserved{false};
        std::atomic<int> shotCommand{0};
        int consumedShotCommand = 0;
        std::atomic<bool> awaitingShotEcho{false};
        std::atomic<std::uint32_t> observedWeapon{INVALID_EHANDLE_INDEX};
        std::atomic<float> observedShotTime{0.f};
        float requestedSpeed = -1.f;

        struct Peek {
            bool enabled = false, moved = false, returning = false, forceReturn = false;
            Vector_t origin{}, input{};
        } peek;

        bool duckOwned = false, duckReturn = false;
        CBaseHandle duckTarget{}, duckWeapon{};
        int duckProvider = 0;
        std::atomic<float> duckCameraHeight{-1.f};

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

        bool LastShotTime(C_CSWeaponBase* weapon, float& time) {
            const auto offset = SchemaFinder::Get(hash_32_fnv1a_const("C_CSWeaponBase->m_fLastShotTime"));
            return weapon && offset && Mem::ReadField(weapon, offset, time) && std::isfinite(time);
        }

        bool DuckWeaponReady(CUserCmd* cmd, C_CSPlayerPawn* local, C_CSWeaponBase* weapon, bool secondary) {
            if (!weapon || weapon->IsNonGunWeapon() || weapon->m_iClip1() <= 0)
                return false;
            const int selected = cmd->csgoUserCmd.pBaseCmd->nWeaponSelect;
            if (selected > 0 && selected != reinterpret_cast<CEntityInstance*>(weapon)->handle().index())
                return false;
            auto* controller = I::GameEntity->Instance->Get<CCSPlayerController>(local->m_hController());
            if (!controller)
                return false;
            double clock = double(controller->m_nTickBase());
            Legitbot::ReadShotClock(cmd, weapon, clock);
            // Ammo and the native attack clock govern firing; the reload animation flag can lag behind both.
            const double next =
                double(secondary ? weapon->m_nNextSecondaryAttackTick() : weapon->m_nNextPrimaryAttackTick()) +
                (secondary ? weapon->m_flNextSecondaryAttackTickRatio() : weapon->m_flNextPrimaryAttackTickRatio());
            if (!std::isfinite(next) || clock + 1e-6 < next)
                return false;
            if (weapon->m_iItemDefinitionIndex() == 64 && !secondary) {
                const int ready = weapon->m_nPostponeFireReadyTicks();
                const float fraction = weapon->m_flPostponeFireReadyFrac();
                // Match SubmitGun's precharge deadline for the command's prediction interval.
                const double intervalEnd = std::floor((std::max)(clock, double(controller->m_nTickBase()))) + 2.;
                if (ready <= 0 || ready == (std::numeric_limits<int>::max)() || !std::isfinite(fraction) ||
                    intervalEnd < double(ready) + fraction)
                    return false;
            }
            return true;
        }

        Vector_t Direction(CBaseUserCmdPB* base) {
            const float yaw = base->pViewAngles->angValue.y * rad;
            Vector_t result{std::cos(yaw) * base->flForwardMove - std::sin(yaw) * base->flSideMove,
                            std::sin(yaw) * base->flForwardMove + std::cos(yaw) * base->flSideMove,
                            0};
            float n = result.Length();
            return n > .001f ? result * (1.f / n) : Vector_t{};
        }

        bool ClearMove(C_CSPlayerPawn* local, const Vector_t& start, const Vector_t& end) {
            Trace::CGameTrace trace{};
            if (!Trace::TraceHull(
                    start, end, {-16, -16, 0}, {16, 16, 72.f - 18.f * Duck(local)}, local, trace, Trace::kMaskWorld))
                return false;
            if (!trace.startsolid() && (!Trace::DidHit(trace) || trace.fraction() > .98f || trace.normal().z > .7f))
                return true;
            // A stair riser blocks a horizontal sweep while native movement can
            // step over it. Reuse Rage's grounded slide/step/support calculation.
            return (local->m_fFlags() & FL_ONGROUND) && Rage::CanAdvanceOnGround(local, start, end);
        }

        bool PeekMove(CUserCmd* cmd,
                      C_CSPlayerPawn* local,
                      const Config::MovementConfig& c,
                      float maximum,
                      bool shot,
                      bool holdReturn,
                      bool forceReturn) {
            auto* base = cmd->csgoUserCmd.pBaseCmd;
            auto* node = local->m_pGameSceneNode();
            if (!node)
                return false;
            const auto origin = node->m_vecAbsOrigin();
            const auto input = Direction(base);
            const float inputLength = input.Length();
            if (!c.quick_peek) {
                peek = {};
                return false;
            }
            if (!c.peek_dt_same_target)
                peek.forceReturn = false;
            if (!peek.enabled) {
                peek = {};
                peek.enabled = true;
                peek.origin = origin;
            }
            const auto delta = peek.origin - origin;
            const float distance = std::hypot(delta.x, delta.y);
            constexpr float arrived = .125f;
            if (distance > arrived && inputLength > .01f)
                peek.moved = true;
            if (!peek.returning && peek.moved &&
                ((c.peek_on_shot && shot) || (c.peek_on_release && inputLength < .01f))) {
                peek.returning = true;
                peek.input = input;
            }
            if (!peek.returning)
                return false;
            const bool takeover = inputLength > .01f &&
                                  (peek.input.Length() < .01f || input.x * peek.input.x + input.y * peek.input.y < .5f);
            if (takeover) {
                peek.returning = false;
                peek.moved = false;
                peek.forceReturn = false;
                return false;
            }
            // Keep the return intent, but leave this command's direction and
            // speed to Rage while a burst or its selected brake pulse is pending.
            // Once a split-target burst triggers the strict policy, finish the
            // return instead of letting a later brake/target steal movement again.
            peek.forceReturn |= forceReturn;
            if (holdReturn && !peek.forceReturn)
                return false;
            if (distance <= arrived) {
                if (!c.peek_stop || std::hypot(local->m_vecVelocity().x, local->m_vecVelocity().y) < 1.f) {
                    peek.returning = false;
                    peek.moved = false;
                    peek.forceReturn = false;
                    if (!c.peek_stop)
                        return false;
                }
                base->flForwardMove = base->flSideMove = 0;
                base->SetBits(BASE_BITS_FORWARDMOVE | BASE_BITS_LEFTMOVE);
                requestedSpeed = 0.f;
                return true;
            }
            const Vector_t direction{delta.x / distance, delta.y / distance, 0};
            const float speed = (std::min)(maximum, delta.Length() * 32.f / std::clamp(c.peek_tolerance, 2, 16));
            if (!ClearMove(local, origin, origin + direction * (std::min)(distance, speed * tick * 2.f))) {
                base->flForwardMove = base->flSideMove = 0;
                base->SetBits(BASE_BITS_FORWARDMOVE | BASE_BITS_LEFTMOVE);
                requestedSpeed = 0.f;
                return true;
            }
            const float yaw = base->pViewAngles->angValue.y * rad;
            base->flForwardMove = direction.x * std::cos(yaw) + direction.y * std::sin(yaw);
            base->flSideMove = -direction.x * std::sin(yaw) + direction.y * std::cos(yaw);
            base->SetBits(BASE_BITS_FORWARDMOVE | BASE_BITS_LEFTMOVE);
            requestedSpeed = speed;
            return true;
        }

        void DuckMove(CUserCmd* cmd, C_CSPlayerPawn* local, C_CSWeaponBase* weapon, bool shot) {
            if (!Config::movement.duck_peek || !(cmd->nButtons.nValue & IN_DUCK)) {
                duckOwned = duckReturn = false;
                duckTarget = {};
                duckCameraHeight.store(-1.f, std::memory_order_relaxed);
                return;
            }
            const auto weaponHandle = reinterpret_cast<CEntityInstance*>(weapon)->handle();
            if (weaponHandle != duckWeapon) {
                duckOwned = duckReturn = false;
                duckTarget = {};
                duckWeapon = weaponHandle;
            }
            const float amount = Duck(local);
            if (!duckOwned && !duckReturn && amount > .99f)
                duckCameraHeight.store(-1.f, std::memory_order_relaxed);
            if (shot && duckOwned)
                duckReturn = true;
            if (duckReturn) {
                InputInject::SetHeld(cmd, IN_DUCK, true);
                if (amount > .99f) {
                    duckReturn = duckOwned = false;
                    duckCameraHeight.store(-1.f, std::memory_order_relaxed);
                }
                return;
            }
            const bool rage = Rage::Active();
            const auto& legit = LegitProfiles::Current();
            if (!rage && !legit.enabled) {
                duckOwned = false;
                duckTarget = {};
                return;
            }
            const auto c = rage ? Rage::Current() : Rage::Settings{};
            const bool manual = (cmd->nButtons.nValue & (IN_ATTACK | IN_SECOND_ATTACK)) != 0;
            const bool secondary =
                weapon->m_iItemDefinitionIndex() == 64 &&
                (rage ? c.force_no_spread && c.auto_fire && !manual && !Rage::NeedsHitchanceFallback(c)
                      : !(cmd->nButtons.nValue & IN_ATTACK) && (cmd->nButtons.nValue & IN_SECOND_ATTACK));
            if (!DuckWeaponReady(cmd, local, weapon, secondary)) {
                InputInject::SetHeld(cmd, IN_DUCK, true);
                duckOwned = false;
                duckTarget = {};
                return;
            }
            const auto* rageTarget = rage ? Rage::PreparedTarget(cmd) : nullptr;
            const auto legitTarget = rage ? Legitbot::Target{} : Legitbot::CurrentTarget();
            const auto handle = rage ? (rageTarget ? rageTarget->handle : CBaseHandle{}) : legitTarget.handle;
            if (!handle.valid()) {
                duckOwned = false;
                duckTarget = {};
                return;
            }
            const int provider = rage ? 1 : 2;
            if (handle != duckTarget || provider != duckProvider) {
                duckOwned = false;
                duckTarget = handle;
                duckProvider = provider;
            }
            auto* target = I::GameEntity->Instance->Get<C_CSPlayerPawn>(handle);
            if (!target || target->m_iHealth() <= 0)
                return;
            const auto eye = Bones::GetEyePos(local);
            const int minimum =
                rage ? c.Minimum(target->m_iHealth())
                     : (std::max)(1,
                                  LegitMath::MinimumDamage(legit.trigger ? legit.min_damage : legit.aim_min_damage,
                                                           target->m_iHealth()));
            const auto damageAt = [&](float height) {
                const auto at = eye + Vector_t{0, 0, height};
                if (rage)
                    return Rage::RayDamage(local, weapon, *rageTarget, at, (rageTarget->point - at).Normalize(), c);
                const bool visible = legit.trigger ? legit.trigger_checks[0] : legit.checks && legit.visible;
                if (visible && !Legitbot::Visible(at, legitTarget.point, local, target))
                    return -1.f;
                return Legitbot::ShotDamage(at, legitTarget.point, legitTarget.group, local, target, weapon);
            };
            if (!duckOwned) {
                if (amount < .98f || damageAt(0) >= minimum || damageAt(18.f) < minimum)
                    return;
                duckCameraHeight.store(local->m_vecViewOffset().z, std::memory_order_relaxed);
                duckOwned = true;
            }
            // Binary duck input cannot hold a partial height. Reversing around a
            // height threshold repeatedly consumes the game's duck-speed budget.
            InputInject::SetHeld(cmd, IN_DUCK, false);
        }
    } // namespace

    float Prepare(CUserCmd* cmd, C_CSPlayerPawn* local) {
        requestedSpeed = -1.f;
        bool shot = shotObserved.exchange(false, std::memory_order_acq_rel);
        if (!Config::movement.quick_peek && !Config::movement.duck_peek) {
            Reset();
            return -1.f;
        }
        auto* base = cmd ? cmd->csgoUserCmd.pBaseCmd : nullptr;
        auto* weapon = local ? local->GetActiveWeapon() : nullptr;
        auto* data = weapon ? weapon->Data() : nullptr;
        if (!base || !base->pViewAngles || !data) {
            duckOwned = duckReturn = false;
            duckTarget = duckWeapon = {};
            duckCameraHeight.store(-1.f, std::memory_order_relaxed);
            return -1.f;
        }
        const int committed = shotCommand.load(std::memory_order_acquire);
        if (committed && committed != consumedShotCommand) {
            consumedShotCommand = committed;
            shot = true;
        }
        float shotTime = 0;
        if (LastShotTime(weapon, shotTime)) {
            const auto handle = reinterpret_cast<CEntityInstance*>(weapon)->handle().raw();
            if (observedWeapon.load() != handle) {
                observedWeapon.store(handle);
                observedShotTime.store(shotTime);
                awaitingShotEcho.store(false);
            } else if (shotTime > observedShotTime.load()) {
                observedShotTime.store(shotTime);
                if (!awaitingShotEcho.exchange(false))
                    shot = true;
            }
        }
        const float maximum = Data(data, "CCSWeaponBaseVData->m_flMaxSpeed", std::clamp(weapon->m_weaponMode(), 0, 1));
        if (maximum <= 0)
            return -1.f;
        DuckMove(cmd, local, weapon, shot);
        const bool pending = Rage::DoubleTapPending(cmd);
        const bool forceReturn = Config::movement.peek_dt_same_target && pending && !Rage::DoubleTapPending(cmd, true);
        const bool holdReturn = pending || (Rage::HasPrepared(cmd) && Rage::MovementLimit() >= 0.f);
        PeekMove(cmd, local, Config::movement, maximum, shot, holdReturn, forceReturn);
        return requestedSpeed;
    }

    void Reset() {
        peek = {};
        duckOwned = duckReturn = false;
        duckTarget = duckWeapon = {};
        duckProvider = 0;
        duckCameraHeight.store(-1.f, std::memory_order_relaxed);
        requestedSpeed = -1.f;
        shotObserved.store(false);
        shotCommand.store(0);
        consumedShotCommand = 0;
        awaitingShotEcho.store(false);
        observedWeapon.store(INVALID_EHANDLE_INDEX);
        observedShotTime.store(0.f);
    }

    void StabilizeCamera(Vector_t& origin) {
        const float height = duckCameraHeight.load(std::memory_order_relaxed);
        if (!Config::movement.duck_peek || height < 0.f)
            return;
        auto* local = H::SafeLocalAlive();
        auto* node = local ? local->m_pGameSceneNode() : nullptr;
        if (!node)
            return;
        // Render height only. Movement, native view offset and firing eyes remain physical.
        origin.z = node->m_vecAbsOrigin().z + height;
    }

    void OnShotCommand(CUserCmd* cmd) {
        if (cmd && cmd->nCommandNumber > 0 && (Config::movement.quick_peek || Config::movement.duck_peek)) {
            awaitingShotEcho.store(true, std::memory_order_release);
            shotCommand.store(cmd->nCommandNumber, std::memory_order_release);
        }
    }

    void OnEvent(void* raw) {
        if (!raw || (!Config::movement.quick_peek && !Config::movement.duck_peek))
            return;
        auto* event = static_cast<IGameEvent*>(raw);
        if (std::strcmp(event->GetName(), "weapon_fire"))
            return;
        auto* local = H::SafeLocalAlive();
        if (!local || !I::GameEntity || !I::GameEntity->Instance)
            return;
        auto* controller = I::GameEntity->Instance->Get<CCSPlayerController>(local->m_hController());
        if (controller && event->GetPlayerController("userid") == controller) {
            if (awaitingShotEcho.load(std::memory_order_acquire))
                return;
            auto* weapon = local->GetActiveWeapon();
            float time = 0;
            if (LastShotTime(weapon, time) &&
                observedWeapon.load() == reinterpret_cast<CEntityInstance*>(weapon)->handle().raw() &&
                time <= observedShotTime.load())
                return;
            shotObserved.store(true, std::memory_order_release);
        }
    }

    void UpdateMarker() {
        const auto& c = Config::movement;
        const auto& color = c.peek_color;
        const bool active = c.quick_peek && peek.enabled && H::SafeLocalAlive();
        World::Particles::PeekMarker(active ? &peek.origin : nullptr,
                                     {color.x, color.y, color.z},
                                     color.w,
                                     c.peek_radius,
                                     c.peek_filled,
                                     c.peek_glow);
    }
} // namespace PeekAssist
