#include "../ragebot/rage.h"
#include "../legitbot/semi_rage.h"
#include "velocity_movement.h"
#include "peek_assist.h"
#include "movement.hpp"
#include "../sdk_prio_a/sdk_prio_a.h"
#include "../trace/trace.h"
#include "../../hooks/hooks.h"
#include "../../interfaces/CCSGOInput/CCSGOInput.h"
#include "../../interfaces/interfaces.h"
#include "../../offsets/offsets.h"
#include "../../../cs2/entity/C_CSWeaponBase/C_CSWeaponBase.h"
#include "../../../cs2/entity/CCSPlayerController/CCSPlayerController.h"
#include <array>
#include <cstring>

namespace Velocity {
    namespace {
        std::uintptr_t g_pawn = 0;
        WeaponContext g_weapon{};
        void* g_traceHull = nullptr;
        void* g_filterInit = nullptr;
        bool g_ready = false;
        std::uint32_t g_processedCommandOffset = 0;

        struct NativeMoveSnapshot {
            std::byte header[4];
            std::uint32_t pawnHandle;
            std::byte beforeVelocity[0x30];
            float velocity[3];
            std::byte beforeOrigin[0x84];
            float origin[3];
            int startTick, endTick;
            float startFraction, endFraction;
        };

        static_assert(offsetof(NativeMoveSnapshot, velocity) == 0x38 && offsetof(NativeMoveSnapshot, origin) == 0xc8);
        static_assert(sizeof(NativeMoveSnapshot) == 0xe4);

        bool ReadCompletedMoveOrigin(C_CSPlayerPawn* pawn,
                                     std::uintptr_t services,
                                     const CUserCmd* cmd,
                                     const systems::prediction::state& pre,
                                     Vector_t& origin) {
            if (!g_processedCommandOffset ||
                memory::read<int>(services + g_processedCommandOffset) != cmd->nCommandNumber - 1 || !I::GameEntity ||
                !I::GameEntity->Instance)
                return false;
            auto* controller = I::GameEntity->Instance->Get<CCSPlayerController>(pawn->m_hController());
            if (!controller)
                return false;
            const int tick = static_cast<int>(controller->m_nTickBase());
            if (tick <= 0)
                return false;

            // Read the RIP-relative MoveData returned by the native getter. Calling
            // the getter would reset this shared scratch and lose the completed move.
            const void* table = nullptr;
            const std::uint8_t* getter = nullptr;
            std::array<std::uint8_t, 15> code{};
            std::uint32_t prologue = 0;
            if (!Mem::Read(reinterpret_cast<const void*>(services), table) || !Mem::ReadField(table, 0x130, getter) ||
                !Mem::ReadField(getter, 0, prologue) || prologue != 0x28ec8348 || !Mem::ReadField(getter, 0x64, code) ||
                code[0] != 0x48 || code[1] != 0x8d || code[2] != 0x05 || code[7] != 0x0f || code[8] != 0x29 ||
                code[9] != 0x05 || code[14] != 0x66)
                return false;
            std::int32_t displacement = 0;
            std::memcpy(&displacement, code.data() + 3, sizeof(displacement));
            const auto* scratch = getter + 0x6b + displacement;
            NativeMoveSnapshot move{}, repeated{};
            if (!Mem::ReadField(scratch, 0, move) || !Mem::ReadField(scratch, 0, repeated) ||
                std::memcmp(&move, &repeated, sizeof(move)) != 0 || move.pawnHandle != pawn->handle().raw() ||
                move.startTick != tick - 1 || move.endTick != tick || move.startFraction != 0.f ||
                move.endFraction != 1.f ||
                memory::read<int>(services + g_processedCommandOffset) != cmd->nCommandNumber - 1 ||
                static_cast<int>(controller->m_nTickBase()) != tick)
                return false;
            const Vector_t velocity{move.velocity};
            const Vector_t position{move.origin};
            // A completed sample can survive a teleport or other pose change. Only
            // use a predecessor still within one tick of the observed trajectory.
            if (!((velocity - pre.networked_velocity).Length() <= .05f) ||
                !((position - pre.networked_origin).Length() <= velocity.Length() * cstypes::tick_interval))
                return false;
            origin = position;
            return true;
        }

        std::atomic<bool> g_stabilizeView{false};
        std::atomic<bool> g_slowwalkEffective{false};
        std::atomic<float> g_slowwalkWheel{0.f};

        struct SdlWheelEvent {
            std::uint32_t type, reserved;
            std::uint64_t timestamp;
            std::uint32_t window, mouse;
            float x, y;
            std::uint32_t direction;
        };

        using SdlFilter = bool(__cdecl*)(void*, void*);
        using SdlGetFilter = bool(__cdecl*)(SdlFilter*, void**);
        using SdlSetFilter = void(__cdecl*)(SdlFilter, void*);
        SdlGetFilter g_getEventFilter = nullptr;
        SdlSetFilter g_setEventFilter = nullptr;
        SdlFilter g_previousEventFilter = nullptr;
        void* g_previousEventUser = nullptr;

        bool __cdecl SlowwalkEventFilter(void*, void* event) {
            H::HookScopeGuard guard;
            guard.arm();
            const auto* wheel = static_cast<const SdlWheelEvent*>(event);
            if (!H::Hooks::s_unloading.load(std::memory_order_acquire) && wheel->type == 0x403 &&
                !g_bMenuOpen.load(std::memory_order_relaxed) && g_slowwalkEffective.load(std::memory_order_relaxed) &&
                keybind.isActive(Config::movement.slowwalk)) {
                const float delta = wheel->y != 0.f ? wheel->y : wheel->x;
                g_slowwalkWheel.fetch_add((wheel->direction == 1 ? -delta : delta) * 10.f, std::memory_order_relaxed);
                return false;
            }
            return g_previousEventFilter ? g_previousEventFilter(g_previousEventUser, event) : true;
        }

        using ReadWeaponViewPunch = void*(__fastcall*)(void*, QAngle_t*, char);
        ReadWeaponViewPunch g_readWeaponViewPunch = nullptr;
    } // namespace

    systems::local::snapshot systems::local::get() const {
        return {g_pawn};
    }

    math::vector3 systems::input::get_view_angles() const {
        QAngle_t a{};
        Input::ReadViewAngles(a);
        return {a.x, a.y, a.z};
    }

    const WeaponContext& CombatContext() {
        return g_weapon;
    }

    std::uintptr_t GlobalVarsAddress() {
        return reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll")) + Offset::Global::dwGlobalVars;
    }

    systems::tracing::player_movement_filter
    systems::tracing::make_player_movement_filter(std::uintptr_t pawn, std::uintptr_t mask, int group) const {
        player_movement_filter f{};
        reinterpret_cast<void(__fastcall*)(void*, void*, std::uint64_t, int)>(g_filterInit)(
            &f, reinterpret_cast<void*>(pawn), mask, group);
        return f;
    }

    systems::tracing::result systems::tracing::trace_player_bbox(const math::vector3& start,
                                                                 const math::vector3& end,
                                                                 const bbox_collision& box,
                                                                 const player_movement_filter& filter,
                                                                 std::uintptr_t services) const {
        Trace::CGameTrace trace{};
        // Current build 14192 ProcessMovement passes services+0x7B0 (upstream used 0x638).
        reinterpret_cast<void(__fastcall*)(
            void*, Trace::CGameTrace*, const void*, const void*, const void*, const void*)>(g_traceHull)(
            reinterpret_cast<void*>(services + 0x7B0), &trace, &start, &end, &box, &filter);
        return {trace.fraction(), trace.normal(), trace.position()};
    }

    bool
    TracePlayerHull(void* pawn, const Vector_t& start, const Vector_t& end, float height, Trace::CGameTrace& trace) {
        if (!g_ready || !pawn)
            return false;
        __try {
            const auto address = reinterpret_cast<std::uintptr_t>(pawn);
            const auto services = memory::read<std::uintptr_t>(address + Offsets::C_BasePlayerPawn_m_pMovementServices);
            if (!services)
                return false;
            // Use the same player mask, collision group and native context as
            // movement. Visibility/bullet traces can miss player-only ramps.
            const auto filter = systems::g_tracing.make_player_movement_filter(
                address, memory::read<std::uintptr_t>(address + 0xd50), 11);
            const systems::tracing::bbox_collision box{{-16, -16, 0}, {16, 16, height}};
            reinterpret_cast<void(__fastcall*)(
                void*, Trace::CGameTrace*, const void*, const void*, const void*, const void*)>(g_traceHull)(
                reinterpret_cast<void*>(services + 0x7B0), &trace, &start, &end, &box, &filter);
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    bool Init() {
        bool ok = true;
        g_processedCommandOffset =
            SchemaFinder::Get(hash_32_fnv1a_const("CPlayer_MovementServices->m_nLastCommandNumberProcessed"));
        Offsets::CBaseEntity_m_nActualMoveType =
            SchemaFinder::Get(hash_32_fnv1a_const("C_BaseEntity->m_nActualMoveType"));
        ok &= Offsets::CBaseEntity_m_nActualMoveType != 0;
        Offsets::CCSPlayerModernJump_m_flLastLandedVelocityZ =
            SchemaFinder::Get(hash_32_fnv1a_const("CCSPlayerModernJump->m_flLastLandedVelocityZ"));
        ok &= Offsets::CCSPlayerModernJump_m_flLastLandedVelocityZ != 0;
        Offsets::CCSPlayerModernJump_m_nLastLandedTick =
            SchemaFinder::Get(hash_32_fnv1a_const("CCSPlayerModernJump->m_nLastLandedTick"));
        ok &= Offsets::CCSPlayerModernJump_m_nLastLandedTick != 0;
        Offsets::CCSPlayer_MovementServices_m_ModernJump =
            SchemaFinder::Get(hash_32_fnv1a_const("CCSPlayer_MovementServices->m_ModernJump"));
        ok &= Offsets::CCSPlayer_MovementServices_m_ModernJump != 0;
        Offsets::CCSPlayer_MovementServices_m_flDuckAmount =
            SchemaFinder::Get(hash_32_fnv1a_const("CCSPlayer_MovementServices->m_flDuckAmount"));
        ok &= Offsets::CCSPlayer_MovementServices_m_flDuckAmount != 0;
        Offsets::CCSPlayer_MovementServices_m_bDucked =
            SchemaFinder::Get(hash_32_fnv1a_const("CCSPlayer_MovementServices->m_bDucked"));
        ok &= Offsets::CCSPlayer_MovementServices_m_bDucked != 0;
        Offsets::CCSPlayer_MovementServices_m_flDuckSpeed =
            SchemaFinder::Get(hash_32_fnv1a_const("CCSPlayer_MovementServices->m_flDuckSpeed"));
        ok &= Offsets::CCSPlayer_MovementServices_m_flDuckSpeed != 0;
        Offsets::CCSPlayer_MovementServices_m_flStamina =
            SchemaFinder::Get(hash_32_fnv1a_const("CCSPlayer_MovementServices->m_flStamina"));
        ok &= Offsets::CCSPlayer_MovementServices_m_flStamina != 0;
        Offsets::CCSWeaponBaseVData_m_flMaxSpeed =
            SchemaFinder::Get(hash_32_fnv1a_const("CCSWeaponBaseVData->m_flMaxSpeed"));
        ok &= Offsets::CCSWeaponBaseVData_m_flMaxSpeed != 0;
        Offsets::CCollisionProperty_m_vecMaxs = SchemaFinder::Get(hash_32_fnv1a_const("CCollisionProperty->m_vecMaxs"));
        ok &= Offsets::CCollisionProperty_m_vecMaxs != 0;
        Offsets::CCollisionProperty_m_vecMins = SchemaFinder::Get(hash_32_fnv1a_const("CCollisionProperty->m_vecMins"));
        ok &= Offsets::CCollisionProperty_m_vecMins != 0;
        Offsets::CGameSceneNode_m_vecAbsOrigin =
            SchemaFinder::Get(hash_32_fnv1a_const("CGameSceneNode->m_vecAbsOrigin"));
        ok &= Offsets::CGameSceneNode_m_vecAbsOrigin != 0;
        Offsets::CPlayer_MovementServices_m_flMaxspeed =
            SchemaFinder::Get(hash_32_fnv1a_const("CPlayer_MovementServices->m_flMaxspeed"));
        ok &= Offsets::CPlayer_MovementServices_m_flMaxspeed != 0;
        Offsets::C_BaseEntity_m_flGravityScale =
            SchemaFinder::Get(hash_32_fnv1a_const("C_BaseEntity->m_flGravityScale"));
        ok &= Offsets::C_BaseEntity_m_flGravityScale != 0;
        Offsets::C_BaseEntity_m_nActualMoveType =
            SchemaFinder::Get(hash_32_fnv1a_const("C_BaseEntity->m_nActualMoveType"));
        ok &= Offsets::C_BaseEntity_m_nActualMoveType != 0;
        Offsets::C_BaseEntity_m_pGameSceneNode =
            SchemaFinder::Get(hash_32_fnv1a_const("C_BaseEntity->m_pGameSceneNode"));
        ok &= Offsets::C_BaseEntity_m_pGameSceneNode != 0;
        Offsets::C_BaseModelEntity_m_Collision =
            SchemaFinder::Get(hash_32_fnv1a_const("C_BaseModelEntity->m_Collision"));
        ok &= Offsets::C_BaseModelEntity_m_Collision != 0;
        Offsets::C_BasePlayerPawn_m_pMovementServices =
            SchemaFinder::Get(hash_32_fnv1a_const("C_BasePlayerPawn->m_pMovementServices"));
        ok &= Offsets::C_BasePlayerPawn_m_pMovementServices != 0;
        g_traceHull =
            M::FindPattern("client", "48 89 74 24 ? 55 57 41 54 41 55 41 56 48 8D AC 24 ? ? ? ? 48 81 EC A0 01 00 00");
        g_filterInit = M::FindPattern("client", "48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 0F B6 41 ? 33 FF C7 41");
        for (const auto& c : kConvars)
            Cvar::Float(c.name, c.value);
        g_readWeaponViewPunch = reinterpret_cast<ReadWeaponViewPunch>(
            M::FindPattern("client", "40 53 48 83 EC 20 48 8B 89 ? ? ? ? 48 8B DA E8"));
        auto sdl = GetModuleHandleW(L"SDL3.dll");
        g_getEventFilter = reinterpret_cast<SdlGetFilter>(GetProcAddress(sdl, "SDL_GetEventFilter"));
        g_setEventFilter = reinterpret_cast<SdlSetFilter>(GetProcAddress(sdl, "SDL_SetEventFilter"));
        g_ready = ok && g_traceHull && g_filterInit && g_readWeaponViewPunch && g_getEventFilter && g_setEventFilter;
        if (g_ready) {
            g_getEventFilter(&g_previousEventFilter, &g_previousEventUser);
            g_setEventFilter(SlowwalkEventFilter, nullptr);
        }
        return g_ready;
    }

    void Reset() {
        PeekAssist::Reset();
        g_slowwalkEffective.store(false, std::memory_order_relaxed);
        g_stabilizeView.store(false, std::memory_order_relaxed);
        using namespace features::movement;
        g_jump_press_window = {};
        g_bhop = {};
        g_airstrafe = {};
        g_jumpbug = {};
        g_fastladder = {};
        g_edgejump = {};
        g_edgestop = {};
        g_edgebug = {};
        g_slowwalk = {};
        g_test_strafer = {};
        systems::g_prediction.current = {};
        g_pawn = 0;
        g_weapon = {};
    }

    void Shutdown() {
        if (g_getEventFilter && g_setEventFilter) {
            SdlFilter filter = nullptr;
            void* user = nullptr;
            if (g_getEventFilter(&filter, &user) && filter == SlowwalkEventFilter)
                g_setEventFilter(g_previousEventFilter, g_previousEventUser);
        }
        g_slowwalkEffective.store(false, std::memory_order_relaxed);
        g_slowwalkWheel.store(0.f, std::memory_order_relaxed);
    }

    void ApplySlowwalkWheel() {
        const float delta = g_slowwalkWheel.exchange(0.f, std::memory_order_relaxed);
        if (delta != 0.f)
            Config::movement.slowwalk_speed = std::clamp(Config::movement.slowwalk_speed + delta, 0.f, 100.f);
    }

    bool Run(CUserCmd* cmd, void* pawnPtr) {
        const float legitSpeed = Legitbot::PlannedSpeedLimit();
        const bool rageEnabled = Rage::Active();
        float semiSpeed = rageEnabled ? Rage::MovementLimit() : (legitSpeed > 0.f ? legitSpeed : -1.f);
        float brakeStart = rageEnabled ? Rage::MovementBrakePhase() : 0.f;
        bool enabled = semiSpeed >= 0.f;
        for (const auto& f : Config::MovementFeatures())
            enabled |= keybind.isActive(*f.enabled);
        if (!g_ready || !enabled || !cmd || !cmd->csgoUserCmd.pBaseCmd || !cmd->csgoUserCmd.pBaseCmd->pViewAngles) {
            Reset();
            return false;
        }
        auto* pawn = static_cast<C_CSPlayerPawn*>(pawnPtr);
        if (!pawn)
            return false;
        const auto services = reinterpret_cast<std::uintptr_t>(pawn->m_pMovementServices());
        if (!services)
            return false;
        const float peekSpeed = PeekAssist::Prepare(cmd, pawn);
        if (peekSpeed >= 0.f) {
            semiSpeed = peekSpeed;
            brakeStart = 0.f;
        }
        g_pawn = reinterpret_cast<std::uintptr_t>(pawn);
        g_stabilizeView.store((keybind.isActive(Config::movement.bhop) && (cmd->nButtons.nValue & IN_JUMP)) ||
                                  (keybind.isActive(Config::movement.airstrafe) && !(pawn->m_fFlags() & FL_ONGROUND)),
                              std::memory_order_relaxed);
        auto& pre = systems::g_prediction.current;
        pre.buttons = cmd->nButtons.nValue;
        pre.flags = pawn->m_fFlags();
        pre.networked_velocity = pawn->m_vecVelocity();
        pre.velocity = pawn->m_vecAbsVelocity();
        // The local pawn is already predicted at this command site. m_vOldOrigin
        // is one simulation tick behind; mixing it with current velocity misses landings.
        auto* node = pawn->m_pGameSceneNode();
        pre.origin = node ? node->m_vecAbsOrigin() : pawn->m_vOldOrigin();
        pre.networked_origin = pre.origin;
        // Native movement resolves a dirty scene transform before it reads the
        // absolute origin. Match that read for airborne landing traces.
        if (node && !(pre.flags & FL_ONGROUND) && pre.networked_velocity.z <= 0.f && (pre.buttons & IN_JUMP) &&
            (keybind.isActive(Config::movement.bhop) || keybind.isActive(Config::movement.jumpbug))) {
            Vector_t movementOrigin{};
            if (SdkPrioA::GetAbsOrigin(pawn, movementOrigin))
                pre.networked_origin = movementOrigin;
            if (ReadCompletedMoveOrigin(pawn, services, cmd, pre, movementOrigin))
                pre.networked_origin = movementOrigin;
        }
        pre.stamina = memory::read<float>(services + Offsets::CCSPlayer_MovementServices_m_flStamina);
        pre.surface_friction = memory::read<float>(services + 0x26C);
        pre.last_movement_impulses = {memory::read<float>(services + 0x1A0),
                                      memory::read<float>(services + 0x1A4),
                                      memory::read<float>(services + 0x1A8)};
        g_weapon = {};
        if (auto* weapon = pawn->GetActiveWeapon())
            if (auto* data = weapon->Data()) {
                g_weapon.weapon_vdata = reinterpret_cast<std::uintptr_t>(data);
                g_weapon.valid = true;
                g_weapon.weapon_max_speed =
                    memory::read<float>(g_weapon.weapon_vdata + Offsets::CCSWeaponBaseVData_m_flMaxSpeed +
                                        sizeof(float) * std::clamp(weapon->m_weaponMode(), 0, 1));
            }
        const bool manualWalk =
            keybind.isActive(Config::movement.slowwalk) && (pre.flags & FL_ONGROUND) && peekSpeed < 0.f;
        const float manualCap =
            g_weapon.weapon_max_speed * .34f * std::clamp(Config::movement.slowwalk_speed, 0.f, 100.f) * .01f;
        const bool manualControls = manualWalk && (semiSpeed < 0.f || manualCap <= semiSpeed);
        if (manualWalk && semiSpeed >= 0.f) {
            semiSpeed = (std::min)(semiSpeed, manualCap);
            brakeStart = 0.f;
        }
        const bool walkingInput =
            cmd->csgoUserCmd.pBaseCmd->flForwardMove != 0.f || cmd->csgoUserCmd.pBaseCmd->flSideMove != 0.f;
        InputInject::ClearAllSubticks(cmd->csgoUserCmd.pBaseCmd);
        using namespace features::movement;
        g_airstrafe.store_angles();
        const bool slowwalkApplied = g_slowwalk.on_create_move(cmd, semiSpeed, brakeStart);
        g_slowwalkEffective.store(slowwalkApplied && manualControls &&
                                      (walkingInput || pre.networked_velocity.length_2d() > .01f),
                                  std::memory_order_relaxed);
        g_edgebug.on_create_move(cmd);
        g_edgejump.on_create_move(cmd);
        g_edgestop.on_create_move(cmd);
        g_jumpbug.on_create_move(cmd);
        g_fastladder.on_create_move(cmd);
        if (!(slowwalkApplied && semiSpeed >= 0.f)) {
            g_test_strafer.on_create_move(cmd);
            if (keybind.isActive(Config::movement.airstrafe) || keybind.isActive(Config::movement.airstop))
                g_airstrafe.on_create_move(cmd);
        }
        auto* base = cmd->csgoUserCmd.pBaseCmd;
        InputInject::EnsureAnalogMoveStep(cmd, pawn);
        g_bhop.on_create_move(cmd);
        bool analogSteps = false;
        for (int i = 0; i < base->subtickMovesField.nCurrentSize; ++i) {
            const auto* step = StepAt(base, i);
            analogSteps |=
                step && step->nButton == 0 &&
                (step->nCachedBits & (MOVESTEP_BITS_ANALOG_FORWARD_DELTA | MOVESTEP_BITS_ANALOG_LEFT_DELTA)) != 0;
        }
        if (analogSteps && ((slowwalkApplied && semiSpeed >= 0.f) || !g_test_strafer.handled_this_tick())) {
            SetForward(base, 0.f);
            SetLeft(base, 0.f);
        }
        InputInject::SanitizeSubticks(base);
        // The hook runs before native button-PB copying and engine CRC generation.
        // The engine itself finalizes the command; no CRC/anti-tamper hook is needed.
        return true;
    }

    bool ReadWeaponRecoil(void* pawn, QAngle_t& out) {
        if (!pawn || !g_readWeaponViewPunch)
            return false;
        __try {
            g_readWeaponViewPunch(pawn, &out, 1);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
        return out.IsValid();
    }

    void StabilizeCamera(float* angles) {
        if (!angles || !g_stabilizeView.load(std::memory_order_relaxed) || g_bMenuOpen)
            return;
        auto* pawn = H::SafeLocalAlive();
        QAngle_t raw{}, recoil{};
        if (!pawn || !Input::ReadViewAngles(raw) || !raw.IsValid())
            return;
        // Use the input view and the engine's weapon-only recoil. Movement yaw
        // and landing punch must not feed back into the player's camera.
        if (!Config::remove_recoil) {
            if (!ReadWeaponRecoil(pawn, recoil))
                return;
        }
        angles[0] = std::clamp(raw.x + recoil.x, -89.f, 89.f);
        angles[1] = std::remainder(raw.y + recoil.y, 360.f);
        angles[2] = 0.f;
    }

    void RestoreViewAngles(const QAngle_t& angles) {
        if (Input::SetViewAngle && Input::pCSGOInput) {
            Vector_t v{angles.x, angles.y, angles.z};
            Input::SetViewAngle(reinterpret_cast<std::uintptr_t>(Input::pCSGOInput), 0, &v);
        }
    }
} // namespace Velocity
