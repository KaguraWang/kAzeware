#include "../features/ragebot/rage.h"
#include "../features/legitbot/profiles.h"
#include "../features/ragebot/melee.h"
#include "../features/legitbot/semi_rage.h"
#include "../features/legitbot/accuracy.h"
#include "hooks.h"
#include "../features/world/utility_range.h"
#include <iostream>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <atomic>
#include <shellapi.h>

#include "../../kAzeware/utils/memory/Interface/Interface.h"
#include "../utils/memory/patternscan/patternscan.h"
#include "../utils/console/console.h"
#include "../utils/crypto/xorstr.h"

#include "../features/visuals/visuals.h"
#include "../features/chams/chams.h"

#include "../../cs2/datatypes/cutlbuffer/cutlbuffer.h"
#include "../../cs2/datatypes/keyvalues/keyvalues.h"
#include "../../cs2/entity/C_Material/C_Material.h"
#include "../../cs2/sdk/IGameEvent.h"

#include "../config/config.h"
#include "../keybinds/keybinds.h"
#include "../interfaces/interfaces.h"
#include "../utils/schema/schema.h"
#include "../utils/fnv1a/fnv1a.h"
#include "../features/world/world.h"
#include "../features/world/weather.h"
#include "../features/hitmarker/hitmarker.h"
#include "../features/bullet_impact/bullet_impact.h"
#include "../features/hitsound/hitsound.h"
#include "../features/sound_esp/sound_esp.h"
#include "../features/vote/vote.h"
#include "../features/panorama/panorama.h"
#include "../features/radar/radar.h"
#include "../features/hitlog/hitlog.h"
#include "../features/visuals/removals/local_removals.h"
#include "../features/scoreboard_weapons/scoreboard_weapons.h"
#include "../features/glow/glow.h"
#include "../features/gamemode/gamemode.h"
#include "../features/grenade_helper/grenade_helper.h"
#include "../features/nadepred/nadepred.h"
#include "../features/prediction/prediction.h"
#include "../features/engine2/engine2.h"
#include "../features/w2s/w2s.h"
#include "../features/bomb/bomb.h"
#include "../features/enemy_spec/enemy_spec.h"
#include "../features/sdk_prio_a/sdk_prio_a.h"
#include "../features/skinchanger/skinchanger.h"

#include "../features/input_inject/input_inject.h"
#include "../features/movement/velocity_movement.h"
#include "../features/movement/peek_assist.h"
#include "../features/legitbot/legitbot.h"
#include "../features/legitbot/trigger.h"
#include "../features/legitbot/events.h"
#include "../../cs2/datatypes/viewmatrix/viewmatrix.h"
#include "../features/bones/bones.h"
#include "../utils/memory/memsafe/memsafe.h"
#include "../offsets/offsets.h"

C_CSPlayerPawn* H::SafeLocalPlayer() noexcept {
    C_CSPlayerPawn* p = nullptr;
    if (oGetLocalPlayer) {
        __try {
            p = oGetLocalPlayer(0);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            p = nullptr;
        }
    }
    // Independent dumper fallback. This also covers a stale function signature
    // whose bytes still match but whose return contract changed.
    if (!p) {
        __try {
            if (HMODULE client = GetModuleHandleA("client.dll")) {
                auto** pp = reinterpret_cast<C_CSPlayerPawn**>(reinterpret_cast<std::uintptr_t>(client) +
                                                               Offset::Global::dwLocalPlayerPawn);
                p = pp ? *pp : nullptr;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            p = nullptr;
        }
    }
    if (!p)
        return nullptr;
    // range+vtable only - no VirtualQuery (hot path)
    const auto a = reinterpret_cast<std::uintptr_t>(p);
    if (a < 0x10000ull || a > 0x00007FFFFFFFFFFFull)
        return nullptr;
    void* vt = nullptr;
    __try {
        vt = *reinterpret_cast<void**>(p);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
    const auto va = reinterpret_cast<std::uintptr_t>(vt);
    if (va < 0x10000ull || va > 0x00007FFFFFFFFFFFull)
        return nullptr;

    // TDM death/respawn: probe health read - free'd pawn fails SEH / garbage hp.
    // Dead pawns still returned (EnemySpec); combat uses SafeLocalAlive.
    int hp = 0;
    __try {
        hp = p->m_iHealth();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
    if (hp < 0 || hp > 200)
        return nullptr;
    return p;
}

// Alive local only - CreateMove / combat / skin apply. Spec uses SafeLocalPlayer.
C_CSPlayerPawn* H::SafeLocalAlive() noexcept {
    C_CSPlayerPawn* p = SafeLocalPlayer();
    if (!p)
        return nullptr;
    int hp = 0;
    std::uint8_t life = 1;
    __try {
        hp = p->m_iHealth();
        life = p->m_lifeState();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
    if (hp <= 0 || hp > 200 || life != 0)
        return nullptr;
    return p;
}

// Dump client_dll.hpp: CPlayer_MovementServices::m_nLastCommandNumberProcessed @ 0x188.
// CreateMove join gate: pawn exists but ProcessMovement hasn't run.
// Schema-first: the Aug-22 update proved frozen dump offsets rot silently.
static bool LocalPawnCmdReady(C_CSPlayerPawn* p) noexcept {
    if (!p || !Mem::ValidEntity(p))
        return false;
    void* move = nullptr;
    __try {
        move = p->m_pMovementServices();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (!move || !Mem::IsUserPtr(move))
        return false;
    static std::uint32_t s_off = 0;
    if (!s_off) {
        s_off = SchemaFinder::Get(hash_32_fnv1a_const("CPlayer_MovementServices->m_nLastCommandNumberProcessed"));
        if (!s_off)
            s_off = 0x188u;
    }
    std::uint32_t last = 0;
    if (!Mem::ReadField(move, s_off, last))
        return false;

    return true; // last==0: ProcessMovement not ticked yet; don't gate combat pipeline
}

// Menu input block hooks - do NOT ShowCursor here (refcount spam); Present owns cursor.
void __fastcall H::hkIsRelativeMouseMode(void* pInputSystem, bool active) {
    H::Hooks::EnterHook();
    if (H::Hooks::s_unloading.load(std::memory_order_acquire)) {
        auto orig = IsRelativeMouseMode.GetOriginal();
        if (orig) {
            __try {
                orig(pInputSystem, active);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }
        H::Hooks::LeaveHook();
        return;
    }
    if (pInputSystem)
        g_pInputSystem = pInputSystem;

    // Track preferred mode for restore-on-close:
    // - Menu closed: trust every game request
    // - Menu open: only latch true (ignore forced-false noise while UI is up)
    if (!g_bMenuOpen.load(std::memory_order_relaxed) || active)
        g_wantRelativeMouse = active;

    auto orig = IsRelativeMouseMode.GetOriginal();
    if (!orig) {
        H::Hooks::LeaveHook();
        return;
    }

    // Menu open: force absolute mouse for ImGui cursor; closed: honor game
    const bool menuOpen = g_bMenuOpen.load(std::memory_order_relaxed);
    __try {
        orig(pInputSystem, menuOpen ? false : active);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Con::Seh("IsRelativeMouseMode", GetExceptionCode());
    }
    H::Hooks::LeaveHook();
}

bool __fastcall H::hkMouseInputEnabled(void* rcx) {
    H::Hooks::EnterHook();
    if (H::Hooks::s_unloading.load(std::memory_order_acquire)) {
        bool ret = true;
        auto orig = MouseInputEnabled.GetOriginal();
        if (orig) {
            __try {
                ret = orig(rcx);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }
        H::Hooks::LeaveHook();
        return ret;
    }
    // Block game mouse look/click while menu is up (ImGui uses WndProc path)
    if (g_bMenuOpen.load(std::memory_order_relaxed)) {
        H::Hooks::LeaveHook();
        return false;
    }

    auto orig = MouseInputEnabled.GetOriginal();
    if (orig) {
        bool ret = true;
        __try {
            ret = orig(rcx);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Con::Seh("MouseInputEnabled", GetExceptionCode());
        }
        H::Hooks::LeaveHook();
        return ret;
    }
    H::Hooks::LeaveHook();
    return true;
}

static void sehFrameStage(void* a1, int stage) {
    (void)a1;
    __try {
        const bool leaving = H::SessionMapLeaving();
        // Park blocks entity/world work until next spawn. Auto-accept still
        // needs OnFrame in the main-menu queue (SessionLive is false there).
        if ((!leaving || !H::SessionLive()) && (stage == FRAME_NET_UPDATE_START || stage == FRAME_RENDER_START)) {
            __try {
                Panorama::OnFrame();
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                Con::Seh("Panorama.OnFrame", GetExceptionCode());
            }
            __try {
                HitLog::Update();
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                Con::Seh("Shot log", GetExceptionCode());
            }
            __try {
                if (stage == FRAME_RENDER_START)
                    LocalRemovals::Update();
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                Con::Seh("Local removals", GetExceptionCode());
            }
        }

        // LevelShutdown can miss (pattern / listen-server). Drain game-thread
        // Aim/Pred wipe here so Present-only leave does not leave stale pawn*.
        if (stage == FRAME_NET_UPDATE_END) {
            World::UtilityRange::Update();
            if (leaving)
                H::SessionDrainGameLeave();
            H::SessionDrainDeath();
        }

        // Spec while dead - must run before EntityOk return.
        if (!leaving && H::SessionEntityReady() && (stage == FRAME_RENDER_START || stage == FRAME_RENDER_END)) {
            if (stage == FRAME_RENDER_START) {
                __try {
                    EnemySpec::OnFrame();
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    Con::Seh("EnemySpec", GetExceptionCode());
                }
            }
        }

        // Weather: signon>=6. Don't wait for a gun or team intro.
        if (!leaving && H::SessionEntityReady() && !H::SessionPostMatch() && stage == FRAME_NET_UPDATE_END) {
            __try {
                SdkPrioA::WarmWorldScan();
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                Con::Seh("SdkPrioA.WarmWorld", GetExceptionCode());
            }
            __try {
                World::WarmScan();
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                Con::Seh("World.WarmScan", GetExceptionCode());
            }
            __try {
                World::Skybox::Tick();
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                Con::Seh("World.Skybox", GetExceptionCode());
            }
            World::Weather::WarmTick();
            PeekAssist::UpdateMarker();
            if (H::SessionLive()) {
                __try {
                    World::Weather::Update();
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    Con::Seh("Weather.FSN_net", GetExceptionCode());
                }
            }
        }

        if (!leaving && stage == FRAME_RENDER_START && H::SessionEntityReady()) {
            EspUpdateVisibility();
            chams::PreparePlayerVisibility();
            chams::UpdateSnapshotChams();
        }

        // Panorama updates its scoreboard tree during render start. Running this
        // at network-update end races the row panels and silently loses the update.
        if (!leaving && H::SessionEntityReady() && !H::SessionPostMatch() && stage == FRAME_RENDER_START) {
            __try {
                ScoreboardWeapons::OnFrameStageNotify();
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                Con::Seh("ScoreboardWeapons", GetExceptionCode());
            }
        }

        // Combat bodies: alive pawn with scene. Intro/weapon wait was join lag.
        if (!H::SessionEntityOk() || leaving)
            return;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Con::Seh("FrameStageNotify", GetExceptionCode());
    }
}

// Full input strip while menu is open (attack, move, look, weapon scroll, etc.)
static void strip_menu_input(CUserCmd* cmd) {
    if (!cmd)
        return;

    // Zero every button path - wheel weapon-switch often lands in nValueScroll
    cmd->nButtons.nValue = 0;
    cmd->nButtons.nValueChanged = 0;
    cmd->nButtons.nValueScroll = 0;

    CBaseUserCmdPB* base = cmd->csgoUserCmd.pBaseCmd;
    if (base) {
        base->flForwardMove = 0.f;
        base->flSideMove = 0.f;
        base->flUpMove = 0.f;
        base->nMousedX = 0;
        base->nMousedY = 0;
        base->nImpulse = 0;
        // Mouse wheel -> weapon select; must clear or scroll still swaps guns
        base->nWeaponSelect = 0;

        // IDA: buttons_pb @ +0x38; skip if unset / obviously bad
        if (base->pInButtonState && reinterpret_cast<uintptr_t>(base->pInButtonState) > 0x10000ull) {
            base->pInButtonState->nValue = 0;
            base->pInButtonState->nValueChanged = 0;
            base->pInButtonState->nValueScroll = 0;
        }

        auto& field = base->subtickMovesField;
        if (field.pRep && field.nCurrentSize > 0 && field.nCurrentSize <= 64 && field.pRep->nAllocatedSize > 0 &&
            field.pRep->nAllocatedSize <= 128) {
            const int n =
                (field.nCurrentSize < field.pRep->nAllocatedSize) ? field.nCurrentSize : field.pRep->nAllocatedSize;
            for (int i = 0; i < n; ++i) {
                CSubtickMoveStep* step = field.pRep->tElements[i];
                if (!step)
                    continue;
                step->nButton = 0;
                step->bPressed = false;
            }
        }
        InputInject::ClearAllSubticks(base);
    }

    cmd->csgoUserCmd.nAttack1StartHistoryIndex = -1;
    cmd->csgoUserCmd.nAttack2StartHistoryIndex = -1;
}

static void sehCreateMovePost(CUserCmd* cmd) {
    __try {
        if (!cmd || !H::SessionEntityReady())
            return;
        auto* pawn = H::SafeLocalAlive();
        if (!LocalPawnCmdReady(pawn))
            return;
        if (g_bMenuOpen) {
            if (!Rage::Active()) {
                strip_menu_input(cmd);
                return;
            }
            if (!Rage::HasPrepared(cmd))
                strip_menu_input(cmd);
        }
        if (!Config::loading.load(std::memory_order_acquire)) {
            NadePred::Finalize(cmd, pawn);
            if (Rage::Active())
                Rage::Execute(cmd, pawn);
            else
                Legitbot::RunTrigger(cmd, pawn, Legitbot::IsWeaponSwitching());
            Rage::RunStandaloneMelee(cmd, pawn);
        }
        if (Config::auto_defuse)
            if (void* c4 = Bomb::PlantedC4Entity())
                Bomb::TryStartDefuse(c4, pawn);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Con::Seh("CreateMove post", GetExceptionCode());
    }
}

using H::HookScopeGuard;

namespace {
    SafetyHookMid g_velocityMovementHook{};
    SafetyHookInline g_viewmodelRotationHook{};
    void* g_velocityMovementTarget = nullptr;

    void DisableViewmodelRotationHook() {
        (void)g_viewmodelRotationHook.disable();
    }

    void RemoveViewmodelRotationHook() {
        g_viewmodelRotationHook = {};
    }

    void DisableVelocityMovementHook() {
        (void)g_velocityMovementHook.disable();
    }

    void RemoveVelocityMovementHook() {
        g_velocityMovementHook = {};
        g_velocityMovementTarget = nullptr;
    }

    void SehViewModelRotation(void* model, float* angles) {
        __try {
            if (!H::Hooks::s_unloading.load(std::memory_order_acquire) &&
                !Config::loading.load(std::memory_order_acquire) && Config::local_visuals &&
                Config::viewmodel_changer && angles) {
                auto* local = H::SafeLocalAlive();
                auto* viewmodel = reinterpret_cast<C_BaseEntity*>(model);
                if (local && viewmodel && viewmodel->m_hOwnerEntity() == local->handle())
                    H::ApplyViewModelRotation(angles);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Con::Seh("Viewmodel rotation", GetExceptionCode());
        }
    }

    void __fastcall ViewModelRotationCommand(void* model, float* origin, float* angles) {
        H::HookScopeGuard guard;
        guard.arm();
        g_viewmodelRotationHook.call<void>(model, origin, angles);
        SehViewModelRotation(model, angles);
    }

    void VelocityMovementCommand(safetyhook::Context& ctx) {
        H::Hooks::EnterHook();
        __try {
            if (!H::Hooks::s_unloading.load(std::memory_order_acquire) && (!g_bMenuOpen || Rage::Active()) &&
                !Config::loading.load(std::memory_order_acquire) && H::SessionEntityReady()) {
                auto* pawn = reinterpret_cast<C_CSPlayerPawn*>(ctx.r12);
                if (pawn == H::SafeLocalAlive() && LocalPawnCmdReady(pawn)) {
                    auto* cmd = reinterpret_cast<CUserCmd*>(ctx.r14);
                    if (g_bMenuOpen)
                        strip_menu_input(cmd);
                    LegitProfiles::Observe(pawn->GetActiveWeapon());
                    if (Rage::Active()) {
                        Legitbot::Reset();
                        Rage::Prepare(cmd, pawn);
                    } else {
                        Rage::Reset();
                        Legitbot::Run(cmd, pawn);
                    }
                    const bool generated = Velocity::Run(cmd, pawn);
                    Rage::AfterMovement(cmd, pawn, generated);
                    NadePred::Run(cmd, pawn, generated);
                } else {
                    Legitbot::Reset();
                    Rage::Reset();
                    Rage::ResetMelee();
                }
            } else {
                Legitbot::Reset();
                Rage::Reset();
                Rage::ResetMelee();
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Con::Seh("Velocity movement", GetExceptionCode());
        }
        H::Hooks::LeaveHook();
    }
} // namespace

// A reused subtick command still has to carry the native pending selection.
// Build 14192 CreateMove reads its packed weapon handle at input+0x228+0x46C.
static void forward_weapon_selection(CUserCmd* cmd, void* input) {
    if (!cmd || !cmd->csgoUserCmd.pBaseCmd || !input || !I::GameEntity || !I::GameEntity->Instance)
        return;
    CBaseHandle selected;
    if (!Mem::ReadField(input, 0x694, selected) || !selected.raw() ||
        !I::GameEntity->Instance->Get<C_CSWeaponBase>(selected))
        return;
    auto* base = cmd->csgoUserCmd.pBaseCmd;
    base->nWeaponSelect = selected.index();
    base->SetBits(BASE_BITS_WEAPON_SELECT);
    cmd->csgoUserCmd.nHasBits |= CSGOUSERCMD_BITS_BASECMD;
}

static bool CreateMoveBody(void* pInput, int slot, bool active) {
    if (H::Hooks::s_unloading.load(std::memory_order_acquire)) {
        bool _ret = true;
        if (auto orig = H::CreateMove.GetOriginal()) {
            __try {
                _ret = orig(pInput, slot, active);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                _ret = true;
            }
        }
        return _ret;
    }

    // Keep the live input context for feature helpers.
    if (pInput)
        Input::pCSGOInput = pInput;

    const bool sessionOk = H::SessionEntityOk() && !H::SessionMapLeaving();
    static bool lastGeneratedResult = true;
    if (slot == 0 && sessionOk && (!g_bMenuOpen || Rage::Active()) &&
        !Config::loading.load(std::memory_order_acquire)) {
        bool movementActive = false;
        for (const auto& feature : Config::MovementFeatures())
            movementActive |= keybind.isActive(*feature.enabled);
        // Velocity's command guard: the engine is revisiting the same subtick
        // command. Regenerating it discards its previously finalized movement.
        movementActive |= Rage::Active() || Legitbot::PlannedSpeedLimit() > 0.f;
        if (movementActive)
            if (auto* cmd = Input::get_user_cmd(0))
                if (cmd->nCmdFlag == 2 && (!Rage::Active() || Rage::HasPrepared(cmd))) {
                    forward_weapon_selection(cmd, pInput);
                    __try {
                        if (auto* pawn = H::SafeLocalAlive(); LocalPawnCmdReady(pawn)) {
                            if (Rage::Active())
                                Rage::Replay(cmd);
                            else {
                                Legitbot::ReplayTrigger(cmd);
                                Rage::RunStandaloneMelee(cmd, pawn);
                            }
                        }
                    } __except (EXCEPTION_EXECUTE_HANDLER) {
                        Con::Seh("CreateMove trigger", GetExceptionCode());
                    }
                    return lastGeneratedResult;
                }
    }

    // MUST return original bool. Dropping RAX (always true) corrupts
    // CCSGOInput / secure join after inject.

    bool origRet = true;
    if (H::CreateMove.IsHooked()) {
        auto original = H::CreateMove.GetOriginal();
        if (original) {
            origRet = original(pInput, slot, active);
        }
    }

    lastGeneratedResult = origRet;
    if (slot != 0)
        return origRet;

    H::SessionDrainDeath();
    if (H::SessionMapLeaving()) {
        H::SessionDrainGameLeave();
        return origRet;
    }
    if (!sessionOk)
        return origRet;
    if (!LocalPawnCmdReady(H::SafeLocalAlive()))
        return origRet;
    CUserCmd* user_cmd = Input::get_user_cmd(0);
    if (!user_cmd)
        return origRet;

    sehCreateMovePost(user_cmd);
    return origRet;
}

bool __fastcall H::hkCreateMove(void* pInput, int slot, bool active) {
    HookScopeGuard guard;
    guard.arm();
    return CreateMoveBody(pInput, slot, active);
}

void __fastcall H::hkHandleViewAngles(void* thisptr, int slot) {
    H::Hooks::EnterHook();
    if (H::Hooks::s_unloading.load(std::memory_order_acquire)) {
        auto original = HandleViewAngles.GetOriginal();
        if (original) {
            __try {
                original(thisptr, slot);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }
        H::Hooks::LeaveHook();
        return;
    }
    QAngle_t saved{};
    const bool have = Input::ReadViewAngles(saved) && saved.IsValid();
    if (HandleViewAngles.IsHooked()) {
        if (auto original = HandleViewAngles.GetOriginal()) {
            __try {
                original(thisptr, slot);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                Con::Seh("HandleViewAngles original", GetExceptionCode());
            }
        }
    }
    if (have)
        Velocity::RestoreViewAngles(saved);
    H::Hooks::LeaveHook();
}

static void FrameStageBody(void* a1, int stage) {
    if (H::Hooks::s_unloading.load(std::memory_order_acquire)) {
        if (auto orig = H::FrameStageNotify.GetOriginal()) {
            __try {
                orig(a1, stage);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }
        return;
    }
    if (stage == FRAME_NET_UPDATE_START || !Config::radar_reveal)
        __try {
            RadarReveal::Restore();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Con::Seh("Radar restore", GetExceptionCode());
        }
    // Null original = pattern miss / disable race - never call through nullptr
    if (H::FrameStageNotify.IsHooked()) {
        if (auto original = H::FrameStageNotify.GetOriginal()) {
            __try {
                original(a1, stage);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                Con::Seh("FrameStageNotify original", GetExceptionCode());
            }
        }
    }
    // Apply after the engine's network/render update. Applying before the
    // original lets the game overwrite item views and models in the same stage.
    __try {
        SkinChanger::OnFrameStageNotify(stage);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Con::Seh("SkinChanger", GetExceptionCode());
    }
    if (stage == FRAME_RENDER_START) {
        __try {
            chams::DrainSnapshotShutdown();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Con::Seh("Snapshot unload drain", GetExceptionCode());
        }
    }
    sehFrameStage(a1, stage);
    if (stage == FRAME_NET_UPDATE_END)
        __try {
            RadarReveal::Update();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Con::Seh("Radar reveal", GetExceptionCode());
        }
}

void __fastcall H::hkFrameStageNotify(void* a1, int stage) {
    HookScopeGuard guard;
    guard.arm();
    FrameStageBody(a1, stage);
}

/* REMOVED-FEATURE-HOOK (not in current hooks.h)
*/

static uintptr_t ReadVfuncSlot(uintptr_t vt, unsigned off) {
    uintptr_t fn = 0;
    __try {
        fn = *reinterpret_cast<uintptr_t*>(vt + off);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        fn = 0;
    }
    return fn;
}

namespace {
    // The bypass only exists to hide local client.dll patches from a VAC-secured
    // client. An explicitly -insecure launch has those checks disabled already, so
    // a signature miss there must not abort the rest of the feature set.
    bool GameLaunchedInsecure() noexcept {
        __try {
            int count = 0;
            wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
            if (!arguments)
                return false;
            bool insecure = false;
            for (int i = 1; i < count; ++i) {
                if (!_wcsicmp(arguments[i], L"-insecure")) {
                    insecure = true;
                    break;
                }
            }
            LocalFree(arguments);
            return insecure;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }
} // namespace

bool H::Hooks::init() {
    s_down.store(false, std::memory_order_release);
    s_unloading.store(false, std::memory_order_release);
    s_hookDepth.store(0, std::memory_order_release);
    int totalHooks = 0, hookedCount = 0;
    bool requiredOk = true;

    uintptr_t weapon_data_addr = M::patternScan(
        XS("client"),
        XS("48 8B 81 ? ? ? ? 85 D2 78 ? 48 83 FA ? 73 ? F3 0F 10 84 90 ? ? ? ? C3 F3 0F 10 80 ? ? ? ? C3 CC CC CC CC"));
    if (weapon_data_addr)
        oGetWeaponData = *reinterpret_cast<int*>(weapon_data_addr + 0x3);
    else
        Con::OffsetMiss("oGetWeaponData");

    uintptr_t base_entity_addr = M::patternScan(XS("client"), XS("4C 8D 49 ? 81 FA"));
    if (base_entity_addr)
        ogGetBaseEntity = reinterpret_cast<decltype(ogGetBaseEntity)>(base_entity_addr);
    else
        Con::PatternMiss("client", "GetBaseEntity");

    uintptr_t local_player_addr = M::patternScan(
        XS("client"),
        XS("48 83 EC 28 83 F9 FF 75 ? 48 8B 0D ? ? ? ? 48 8D 54 24 30 48 8B 01 FF 90 ? ? ? ? 8B 08 48 63 C1 4C 8D 05"));
    if (local_player_addr)
        oGetLocalPlayer = reinterpret_cast<decltype(oGetLocalPlayer)>(local_player_addr);
    else
        Con::PatternMiss("client", "GetLocalPlayer");

    // UC / IDA CCSGOInput::CreateMove - builds usercmd + subticks (not the old CHLClient-style fn)
    // pattern: 85 D2 0F 85 ? ? ? ? 48 8B C4 44 88 40 (IDA 0x180B09520)
    uintptr_t create_move_addr = M::patternScan(XS("client"), XS("85 D2 0F 85 ? ? ? ? 48 8B C4 44 88 40"));
    if (create_move_addr) {
        ++totalHooks;
        if (!CreateMove.Add(reinterpret_cast<void*>(create_move_addr), reinterpret_cast<void*>(&hkCreateMove))) {
            Con::Error("CreateMove hook.Add failed @ 0x%p", (void*)create_move_addr);
            requiredOk = false;
        } else {
            Con::Ok("CreateMove (CCSGOInput) @ 0x%p", (void*)create_move_addr);
            ++hookedCount;
        }
    } else {
        Con::PatternMiss("client", "CreateMove");
        requiredOk = false;
    }

    // Current CreateMove: generated native buttons, before validation/PB copy/CRC.
    // R14 is the current CUserCmd; R12 is the local pawn at this exact site.
    g_velocityMovementTarget =
        M::FindPattern("client", "48 8B 54 24 30 48 8B 4C 24 78 E8 ? ? ? ? 49 8B 07 49 8B CF 48 8B 54 24 30 FF 50 30");
    if (g_velocityMovementTarget) {
        auto hook = safetyhook::MidHook::create(g_velocityMovementTarget, VelocityMovementCommand);
        if (hook)
            g_velocityMovementHook = std::move(*hook);
        else
            requiredOk = false;
    } else {
        Con::PatternMiss("client", "VelocityMovementCommand");
        requiredOk = false;
    }

    auto addHook = [&](auto& hook, uintptr_t addr, auto detour, const char* name, const char* mod = "client") {
        if (!addr) {
            Con::PatternMiss(mod, name);
            if (std::string(name) == "FrameStageNotify")
                requiredOk = false;
            return;
        }
        ++totalHooks;
        if (!hook.Add(reinterpret_cast<void*>(addr), reinterpret_cast<void*>(detour))) {
            Con::Error("%s hook.Add failed @ 0x%p", name, (void*)addr);
            if (std::string(name) == "FrameStageNotify")
                requiredOk = false;
        } else {
            Con::Ok("%s @ 0x%p", name, (void*)addr);
            ++hookedCount;
        }
    };
    auto addHookOptional = [&](auto& hook, uintptr_t addr, auto detour, const char* name, const char* mod = "client") {
        if (!addr) {
            Con::PatternMiss(mod, name);
            return;
        }
        ++totalHooks;
        if (!hook.Add(reinterpret_cast<void*>(addr), reinterpret_cast<void*>(detour)))
            Con::Error("%s hook.Add failed @ 0x%p", name, (void*)addr);
        else {
            Con::Ok("%s @ 0x%p", name, (void*)addr);
            ++hookedCount;
        }
    };

    {
        uintptr_t hit = M::patternScan(XS("client"), XS("FF FF FF FF 48 8D 05 ? ? ? ? 48 89 0D ? ? ? ?"));
        uintptr_t fn = 0;
        if (hit) {
            std::uint8_t* disp = reinterpret_cast<std::uint8_t*>(hit + 7);
            uintptr_t vt = reinterpret_cast<uintptr_t>(M::GetAbsoluteAddress(disp, 0));
            if (vt)
                fn = ReadVfuncSlot(vt, 0x40);
        }
        addHook(HandleViewAngles, fn, &hkHandleViewAngles, "HandleViewAngles");
    }

    addHook(FrameStageNotify,
            M::patternScan(XS("client"),
                           XS("48 89 5C 24 ? 48 89 6C 24 ? 57 48 83 EC ? 48 8B F9 33 ED 48 8B 89 ? ? ? ? 8B DA")),
            &hkFrameStageNotify,
            "FrameStageNotify");
    World::Weather::Install();
    Hitmarker::Install();
    Legitbot::InitAccuracy();
    if (!Legitbot::InstallEvents())
        Con::Warn("Legitbot event listener unavailable");
    BulletFx::Install();
    Hitsound::Install();
    SoundEsp::Install();
    Vote::Install();
    Panorama::Install();
    addHook(GetRenderFov,
            M::patternScan(XS("client"), XS("40 53 48 83 EC ? 48 8B D9 E8 ? ? ? ? 48 85 C0 74 ? 48 8B C8 48 83 C4")),
            &hkGetRenderFov,
            "GetRenderFov");
    // CALCVIEWMODEL - viewmodel XYZ + FOV (bypass engine clamps after original)
    addHook(GetViewModelOffsets,
            M::patternScan(XS("client"), XS("40 55 53 56 41 56 41 57 48 8B EC 48 83 EC 20 4D")),
            &hkGetViewModelOffsets,
            "GetViewModelOffsets");
    // CalcViewModelView(model, origin, angles): compose after native bob/recoil
    // has finished writing the output angles consumed by the model transform.
    if (auto target = M::FindPattern(
            "client",
            "48 8B C4 55 56 57 41 57 48 8D A8 28 FF FF FF 48 81 EC B8 01 00 00 48 8B F9 49 8B F0 8B 89 20 05 00 00 4C 8B FA")) {
        auto hook = safetyhook::InlineHook::create(target, ViewModelRotationCommand);
        if (hook)
            g_viewmodelRotationHook = std::move(*hook);
        else
            Con::Error("Viewmodel rotation hook failed");
    } else
        Con::PatternMiss("client", "Viewmodel rotation");
    // GetScreenAspectRatio - verified against engine2 IVEngineClient slot 88, build 14192.
    addHook(
        GetScreenAspectRatio,
        M::patternScan(XS("engine2"), XS("48 83 EC 28 48 8D 0D ? ? ? ? E8 ? ? ? ? 0F 57 C9 0F 2F C1 76 10 48 8D 0D")),
        &hkGetScreenAspectRatio,
        "GetScreenAspectRatio",
        "engine2");
    // ClientModeCSNormal slot 16: CViewSetup origin/angles, build 14192.
    {
        uintptr_t ov = M::patternScan(
            XS("client"), XS("40 53 57 48 83 EC 58 48 8B FA E8 ? ? ? ? 48 8B 05 ? ? ? ? 80 78 58 00 0F 84"));
        addHook(OverrideView, ov, &hkOverrideView, "OverrideView");
    }
    // SetupFog - IDA 0x18027D8B0; writes gradient_fog shader params
    addHook(
        SetupFog,
        M::patternScan(
            XS("client"),
            XS("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 48 89 4C 24 ? 57 41 54 41 55 41 56 41 57 48 83 EC 20 48 63 02")),
        &hkSetupFog,
        "SetupFog");
    // DRAWSCOPEOVERLAY - sniper scope HUD out-struct (bars / blur / texture)
    addHook(DrawScopeOverlay,
            M::patternScan(XS("client"), XS("48 8B C4 53 57 48 83 EC ? 48 8B FA")),
            &hkDrawScopeOverlay,
            "DrawScopeOverlay");

    W2S::Init();
    // GetMatrixForView dump (RVA 0x1666C0) is a 3-arg FOV helper - NOT matrix
    // capture. Hooking it as 6-arg poisoned W2S live matrix / stack. Use
    // ScreenTransform + pViewMatrix instead (see w2s.cpp).
    // DrawCrosshair - reticle allow-gate (rifles / knives). Unique dump pattern.
    addHook(DrawCrosshair,
            M::patternScan(XS("client"), XS("48 89 5C 24 08 57 48 83 EC 20 48 8B D9 E8 ? ? ? ? 48 85")),
            &hkDrawCrosshair,
            "DrawCrosshair");

    // FlashOverlay - dump FlashOverlay; IDA 0x18113C960 "FlashbangOverlay"
    addHook(RenderFlashBangOverlay,
            M::patternScan(XS("client"), XS("85 D2 0F 88 ? ? ? ? 48 89 4C 24 08 55 56")),
            &hkRenderFlashbangOverlay,
            "FlashOverlay");
    // DRAWLEGS - Firstperson Legs
    addHook(DrawLegs,
            M::patternScan(XS("client"),
                           XS("4C 8B DC 55 53 56 57 41 56 49 8D AB 78 FB FF FF 48 81 EC 60 05 00 00 8B 42 24")),
            &hkDrawLegs,
            "DrawLegs");
    // DRAWSMOKEVERTEX - smoke volume
    addHook(DrawSmokeVertex,
            M::patternScan(
                XS("client"),
                XS("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 41 56 41 57 48 83 EC ? 48 8B 9C 24 ? ? ? ? 4D 8B F8")),
            &hkDrawSmokeVertex,
            "DrawSmokeVertex");
    // Build 14192: SmokeConstantBuffer lock path (batch draw).
    {
        uintptr_t smokeArr = M::patternScan(
            XS("client"),
            XS("48 89 54 24 10 55 57 41 54 48 8D AC 24 40 F9 FF FF 48 81 EC C0 07 00 00 48 8B 05 ? ? ? ? 4C 8B E2"));
        addHook(DrawSmokeArray, smokeArr, &hkDrawSmokeArray, "DrawSmokeArray");
    }
    // RENDERDECALS - bullet / blood / explosion decals
    addHook(RenderDecals,
            M::patternScan(XS("client"),
                           XS("44 88 4C 24 20 55 53 41 54 41 55 48 8D 6C 24 C1 48 81 EC C8 00 00 00 4C 8B 15")),
            &hkRenderDecals,
            "RenderDecals");
    // CacheParticleEffect - real particle spawn (IDA 0x18078EE10). Old CreateParticleEffect = SetCP.
    addHook(CacheParticleEffect,
            M::patternScan(XS("client"), XS("4C 8B DC 53 48 81 EC 90 00 00 00 F2 0F 10 05")),
            &hkCacheParticleEffect,
            "CacheParticleEffect");
    // ParticleDrawArray - particles.dll fire/molly tint (UC Particle Modulation, IDA 0x1802826D0)
    // Write RGB floats at a2+0x50 before draw. Name filter in detour (inferno_fx / groundfire).
    // Try particles.dll first, then client.dll fallback; pattern shifts per build.
    {
        uintptr_t pda = M::patternScan(XS("particles"), XS("48 89 5C 24 ? 4C 89 4C 24 ? 4C 89 44 24 ? 55"));
        const char* pdaMod = "particles";
        if (!pda) {
            pda = M::patternScan(XS("particles"), XS("48 89 5C 24 ? 48 89 6C 24 ? 56 57 41 56"));
            if (pda)
                pdaMod = "particles#2";
        }
        if (!pda) {
            pda = M::patternScan(XS("client"), XS("48 89 5C 24 ? 4C 89 4C 24 ? 4C 89 44 24 ? 55"));
            if (pda)
                pdaMod = "client";
        }
        if (!pda) {
            // wildcard looser: push rbp variant seen on some builds
            pda = M::patternScan(XS("particles"), XS("40 53 55 56 57 41 56 48 81 EC ? ? ? ?"));
            if (pda)
                pdaMod = "particles#3";
        }
        addHook(ParticleDrawArray, pda, &hkParticleDrawArray, "ParticleDrawArray", pdaMod);
        if (!pda)
            Con::PatternMiss(pdaMod, "ParticleDrawArray (fire/inferno tint will not work)");
    }

    addHook(MouseInputEnabled,
            M::patternScan(XS("client"), XS("40 53 48 83 EC 20 80 B9 BC 0B 00 00 00 48 8B D9 0F 85")),
            &hkMouseInputEnabled,
            "MouseInputEnabled");
    addHook(
        IsRelativeMouseMode,
        M::patternScan(XS("inputsystem"), XS("48 89 6C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 41 56 48 83 EC ? 0F B6 F2")),
        &hkIsRelativeMouseMode,
        "IsRelativeMouseMode",
        "inputsystem");
    addHook(DrawGlow,
            M::patternScan(XS("client"), XS("40 53 48 83 EC 20 48 8B 54 24 28 48 8D 59 30 48 8B CB E8 ? ? ? ? 8B 03")),
            &hkDrawGlow,
            "DrawGlow");
    // GetGlowColor - last-mile float4 before ManageGlow; isolates glow from mesh tint
    addHook(
        GetGlowColor,
        M::patternScan(
            XS("client"),
            XS("48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 20 48 8B F2 48 8B F9 48 8B 54 24 28 48 83 C1 40")),
        &hkGetGlowColor,
        "GetGlowColor");
    // ManageGlowSceneObject - force pure glow colour + backface mult = 1.0f
    addHook(ManageGlowSceneObject,
            M::patternScan(
                XS("client"),
                XS("48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48 89 7C 24 20 41 56 48 83 EC 60 48 8B F2 45 33 F6")),
            &hkManageGlowSceneObject,
            "ManageGlowSceneObject");
    // ApplyGlowScene (B04B30) - pre-stamp + post force scene float4
    addHook(ApplyGlowScene,
            M::patternScan(XS("client"), XS("48 89 5C 24 ? 48 89 6C 24 ? 48 89 7C 24 ? 41 56 48 81 EC 80 00 00 00")),
            &hkApplyGlowScene,
            "ApplyGlowScene");
    addHook(FireEventClientSide,
            M::patternScan(XS("client"), XS("40 53 56 41 54 48 83 EC 30 48 8B F2 48 8D 99 E0 00 00 00 4C 8B E1")),
            &hkFireEventClientSide,
            "FireEventClientSide");
    // Andromeda Hook_AntiTamper - client 40 53 41 57 48 83 EC ? 48 89 74 24 ? 48 8B F1
    // 2nd-queue VAC "launched insecure" check. Detour always returns false.
    addHook(AntiTamper,
            M::patternScan(XS("client"), XS("40 53 41 57 48 83 EC ? 48 89 74 24 ? 48 8B F1")),
            &hkAntiTamper,
            "AntiTamper");
    // A live client runs VAC over these patches; if the bypass is unavailable
    // there is nothing safe to fall back to, so init must roll the DLL back.
    if (!AntiTamper.IsHooked() && !GameLaunchedInsecure()) {
        Con::Error("AntiTamper bypass unavailable on a live (VAC) client - aborting init");
        requiredOk = false;
    }
    // Offline test build: leave the engine's security checks intact.
    // UnlockInventory (CCSInventoryManager::IsLoadoutAllowed) and GetServerLoadoutItem
    // are NOT hooked to prevent Valve's mmqueue tamper check from flagging client code bytes.
    // yougey wetness/rain: CMapInfo env rain strength + wetness coverage
    // Dump SetupMapInfo - wildcard stack saves; hard E8 broke on some builds
    uintptr_t setupMap = M::patternScan(
        XS("client"),
        XS("48 8B C4 48 89 58 ? 48 89 68 ? 48 89 70 ? 57 48 81 EC ? ? ? ? 0F 29 70 ? 48 8B EA 0F 29 78 ? 45 33 C0"));

    // Priority A: entity add/remove (gen bump) + LevelShutdown map unload
    if (void* a = SdkPrioA::OnAddAddr()) {
        addHook(OnAddEntity, reinterpret_cast<uintptr_t>(a), &hkOnAddEntity, "OnAddEntity");
        if (H::OnAddEntity.IsHooked())
            SdkPrioA::MarkHooked("OnAddEntity", "gen bump only");
    }
    if (void* a = SdkPrioA::OnRemoveAddr()) {
        addHook(OnRemoveEntity, reinterpret_cast<uintptr_t>(a), &hkOnRemoveEntity, "OnRemoveEntity");
        if (H::OnRemoveEntity.IsHooked())
            SdkPrioA::MarkHooked("OnRemoveEntity", "gen bump only");
    }
    if (void* a = SdkPrioA::LevelShutdownAddr()) {
        addHook(LevelShutdown, reinterpret_cast<uintptr_t>(a), &hkLevelShutdown, "LevelShutdown");
        if (H::LevelShutdown.IsHooked())
            SdkPrioA::MarkHooked("LevelShutdown", "map gen + feature cleanup");
    }
    Con::Ok("Hooks: %d/%d hooked, required=%s", hookedCount, totalHooks, requiredOk ? "ok" : "FAIL");
    return requiredOk;
}

void H::Hooks::InstallSceneWorldHooks() {
    static std::atomic<bool> s_done{false};
    static std::atomic<bool> s_installing{false};
    static std::atomic<bool> s_sortAttempted{false};
    static std::atomic<bool> s_skyAttempted{false};
    static std::atomic<bool> s_worldAttempted{false};
    static std::atomic<int> s_chamsTries{0};
    if (s_done.load(std::memory_order_acquire))
        return;
    if (!GetModuleHandleA("scenesystem.dll"))
        return;
    if (!H::SessionEntityReady() || H::SessionMapLeaving() || H::SessionPostMatch())
        return;
    bool expected = false;
    if (!s_installing.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return;

    struct InstallGuard {
        std::atomic<bool>& flag;

        ~InstallGuard() {
            flag.store(false, std::memory_order_release);
        }
    } guard{s_installing};

    // mercey sort_primitives - overlay chams drawn last (scenesystem)
    if (!s_sortAttempted.exchange(true, std::memory_order_acq_rel)) {
        uintptr_t sp = M::patternScan(XS("scenesystem"), XS("45 85 C9 0F 84 ? ? ? ? 55 56 57 48 83 EC 30"));
        if (sp && SortPrimitives.Add(reinterpret_cast<void*>(sp), reinterpret_cast<void*>(&hkSortPrimitives)))
            Con::Ok("SortPrimitives @ 0x%p (deferred)", (void*)sp);
        else
            Con::PatternMiss("scenesystem", "SortPrimitives (chams overlay sorting)");
    }

    // Chams materials - CreateMaterial/SetTypeKV3 live once scenesystem ready.
    // Only retry while the one-shot sig attempts are still pending; once they
    // are done this is the sole remaining work, so bound the retries and park.
    if (!chams::Materials::ready())
        chams::Materials::init();

    if (!s_skyAttempted.exchange(true, std::memory_order_acq_rel)) {
        uintptr_t sky =
            M::patternScan(XS("scenesystem"),
                           XS("45 85 C9 0F 8E ? ? ? ? 4C 8B DC 55 41 56 49 8D AB 58 FC FF FF 48 81 EC 98 04 00 00"));
        if (sky && DrawSkyboxArray.Add(reinterpret_cast<void*>(sky), reinterpret_cast<void*>(&hkDrawSkyboxArray)))
            Con::Ok("DrawSkyboxArray @ 0x%p (deferred)", (void*)sky);
        else
            Con::PatternMiss("scenesystem", "DrawSkyboxArray");
    }
    if (!s_worldAttempted.exchange(true, std::memory_order_acq_rel)) {
        World::InstallMapColorHook();
        World::InstallLightingHook();
    }

    if (s_sortAttempted.load(std::memory_order_acquire) && s_skyAttempted.load(std::memory_order_acquire) &&
        s_worldAttempted.load(std::memory_order_acquire)) {
        if (chams::Materials::ready()) {
            if (chams::Materials::warm_hit_opacity())
                s_done.store(true, std::memory_order_release);
        } else if (s_chamsTries.fetch_add(1, std::memory_order_relaxed) >= 300) {
            // ~300 frames of retries failed - park instead of paying the full
            // module lookup + session probes every frame for the whole session.
            Con::Warn("chams materials unresolved after retries - scene hooks parked");
            s_done.store(true, std::memory_order_release);
        }
    }
}

static void OnAddEntityBody(void* entitySystem, void* entity, int handle) {
    if (H::Hooks::s_unloading.load(std::memory_order_acquire)) {
        if (auto orig = H::OnAddEntity.GetOriginal()) {
            __try {
                orig(entitySystem, entity, handle);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }
        return;
    }
    if (H::OnAddEntity.IsHooked()) {
        auto original = H::OnAddEntity.GetOriginal();
        if (original) {
            __try {
                original(entitySystem, entity, handle);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                TW_SEH_CATCH("OnAddEntity.orig");
            }
        }
    }
    __try {
        SdkPrioA::OnEntityAdded(entitySystem, entity, handle);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        TW_SEH_CATCH("OnAddEntity.post");
    }
}

void __fastcall H::hkOnAddEntity(void* entitySystem, void* entity, int handle) {
    HookScopeGuard guard;
    guard.arm();
    OnAddEntityBody(entitySystem, entity, handle);
}

static void OnRemoveEntityBody(void* entitySystem, void* entity, int handle) {
    if (H::Hooks::s_unloading.load(std::memory_order_acquire)) {
        if (auto orig = H::OnRemoveEntity.GetOriginal()) {
            __try {
                orig(entitySystem, entity, handle);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }
        return;
    }
    __try {
        SdkPrioA::OnEntityRemoved(entitySystem, entity, handle);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        TW_SEH_CATCH("OnRemoveEntity.pre");
    }
    if (H::OnRemoveEntity.IsHooked()) {
        auto original = H::OnRemoveEntity.GetOriginal();
        if (original) {
            __try {
                original(entitySystem, entity, handle);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                TW_SEH_CATCH("OnRemoveEntity.orig");
            }
        }
    }
}

void __fastcall H::hkOnRemoveEntity(void* entitySystem, void* entity, int handle) {
    HookScopeGuard guard;
    guard.arm();
    OnRemoveEntityBody(entitySystem, entity, handle);
}

static void* LevelShutdownBody(void* a1) {
    if (H::Hooks::s_unloading.load(std::memory_order_acquire)) {
        void* _ret = nullptr;
        if (auto orig = H::LevelShutdown.GetOriginal()) {
            __try {
                _ret = orig(a1);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                _ret = nullptr;
            }
        }
        return _ret;
    }
    __try {
        chams::ClearSnapshotChams();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Con::Seh("Snapshot level shutdown", GetExceptionCode());
    }
    __try {
        H::SessionOnMapLeave();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        TW_SEH_CATCH("LevelShutdown.pre");
    }
    void* _ret = nullptr;
    if (H::LevelShutdown.IsHooked()) {
        auto original = H::LevelShutdown.GetOriginal();
        if (original) {
            __try {
                _ret = original(a1);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                _ret = nullptr;
            }
        }
    }
    return _ret;
}

void* __fastcall H::hkLevelShutdown(void* a1) {
    HookScopeGuard guard;
    guard.arm();
    return LevelShutdownBody(a1);
}

/* REMOVED-FEATURE-HOOK (not in current hooks.h)
*/

void* __fastcall H::hkDrawGlow(void* glowProp) {
    H::Hooks::EnterHook();
    void* ret = nullptr;
    const bool unloading = H::Hooks::s_unloading.load(std::memory_order_acquire);
    if (DrawGlow.IsHooked()) {
        auto original = DrawGlow.GetOriginal();
        if (original) {
            __try {
                ret = original(glowProp);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }
    }
    if (!unloading && H::SessionEntityReady() && !H::SessionMapLeaving() && !H::SessionPostMatch()) {
        __try {
            Glow::OnDrawGlow(reinterpret_cast<Glow::CGlowProperty*>(glowProp));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Con::Seh("hkDrawGlow", GetExceptionCode());
        }
    }
    H::Hooks::LeaveHook();
    return ret;
}

void __fastcall H::hkGetGlowColor(void* glowProp, float* outRgba) {
    H::Hooks::EnterHook();
    const bool unloading = H::Hooks::s_unloading.load(std::memory_order_acquire);
    if (!unloading && Glow::AnyEnabled() && H::SessionEntityReady() && !H::SessionMapLeaving() &&
        !H::SessionPostMatch()) {
        __try {
            if (Glow::ForceSceneColor(reinterpret_cast<Glow::CGlowProperty*>(glowProp), outRgba)) {
                H::Hooks::LeaveHook();
                return;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Con::Seh("hkGetGlowColor force", GetExceptionCode());
        }
    }
    if (unloading || GetGlowColor.IsHooked()) {
        auto original = GetGlowColor.GetOriginal();
        if (original) {
            __try {
                original(glowProp, outRgba);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                TW_SEH_CATCH("GetGlowColor.orig");
            }
        }
    }
    H::Hooks::LeaveHook();
}

void* __fastcall H::hkManageGlowSceneObject(
    void** glowSceneOut, void* a2, void* sceneNode, float* color4, int a5, int a6, int a7, int a8) {
    H::Hooks::EnterHook();
    // a8 is m_flGlowBackfaceMult - forced to 1.0f only while glow features are
    // active; otherwise pass the game's value through untouched.
    const bool glowGate = !H::Hooks::s_unloading.load(std::memory_order_acquire) && Glow::AnyEnabled() &&
                          H::SessionEntityReady() && !H::SessionMapLeaving() && !H::SessionPostMatch();
    int backface = glowGate ? 0x3F800000 /*1.0f*/ : a8;
    if (glowGate) {
        __try {
            Glow::ForceManageColor(color4);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            TW_SEH_CATCH("ManageGlow.force");
        }
    }
    if (ManageGlowSceneObject.IsHooked()) {
        auto original = ManageGlowSceneObject.GetOriginal();
        if (original) {
            void* ret = nullptr;
            __try {
                ret = original(glowSceneOut, a2, sceneNode, color4, a5, a6, a7, backface);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                ret = nullptr;
            }
            H::Hooks::LeaveHook();
            return ret;
        }
    }
    H::Hooks::LeaveHook();
    return nullptr;
}

std::int64_t __fastcall H::hkApplyGlowScene(void* glowProp, void* sceneNode) {
    H::Hooks::EnterHook();
    using Fn = std::int64_t(__fastcall*)(Glow::CGlowProperty*, void*);
    Fn original = nullptr;
    if (ApplyGlowScene.IsHooked())
        original = reinterpret_cast<Fn>(ApplyGlowScene.GetOriginal());
    if (!H::Hooks::s_unloading.load(std::memory_order_acquire) && Glow::AnyEnabled() && H::SessionEntityReady() &&
        !H::SessionMapLeaving() && !H::SessionPostMatch()) {
        std::int64_t ret =
            Glow::OnApplyGlowScene(reinterpret_cast<Glow::CGlowProperty*>(glowProp), sceneNode, original);
        H::Hooks::LeaveHook();
        return ret;
    }
    if (original) {
        std::int64_t ret = 0;
        __try {
            ret = original(reinterpret_cast<Glow::CGlowProperty*>(glowProp), sceneNode);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            ret = 0;
        }
        H::Hooks::LeaveHook();
        return ret;
    }
    H::Hooks::LeaveHook();
    return 0;
}

bool __fastcall H::hkFireEventClientSide(void* eventManager, void* gameEvent) {
    H::Hooks::EnterHook();
    const bool unloading = H::Hooks::s_unloading.load(std::memory_order_acquire);
    const bool leaving = H::SessionMapLeaving();
    const bool sessionOk = !unloading && H::SessionEntityOk() && !leaving;
    if (sessionOk) {
        __try {
            World::Weather::OnGameEvent(gameEvent);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            TW_SEH_CATCH("FECS.Weather");
        }
        __try {
            Hitmarker::OnGameEvent(gameEvent);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            TW_SEH_CATCH("FECS.Hitmarker");
        }
        if (!Legitbot::EventsActive()) {
            __try {
                Rage::OnEvent(gameEvent);
                if (!Rage::Active())
                    Legitbot::OnGameEvent(gameEvent);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                TW_SEH_CATCH("FECS.Legitbot");
            }
        }
        if (!BulletFx::ListenerActive())
            __try {
                BulletFx::OnGameEvent(gameEvent);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                TW_SEH_CATCH("FECS.BulletFx");
            }
        __try {
            SoundEsp::OnGameEvent(gameEvent);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            TW_SEH_CATCH("FECS.SoundEsp");
        }
        __try {
            Vote::OnGameEvent(gameEvent);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            TW_SEH_CATCH("FECS.Vote");
        }
        __try {
            SkinChanger::OnFireEventClientSide(gameEvent);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            TW_SEH_CATCH("FECS.SkinChanger");
        }
    }

    if (FireEventClientSide.IsHooked()) {
        auto original = FireEventClientSide.GetOriginal();
        if (original) {
            bool ret = false;
            __try {
                ret = original(eventManager, gameEvent);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                Con::Seh("FireEventClientSide original", GetExceptionCode());
            }
            H::Hooks::LeaveHook();
            return ret;
        }
    }
    H::Hooks::LeaveHook();
    return false;
}

bool __fastcall H::hkAntiTamper(void* /*src*/, int /*a2*/, __int64 /*a3*/, int /*a4*/) {
    // Installed detour - must track depth like every other hook (quiescence
    // contract, hooks.h). Body never calls the original or touches state.
    H::Hooks::EnterHook();
    H::Hooks::LeaveHook();
    return false;
}

// ============================================================================
// Session gates + teardown (declared in hooks.h). Contracts from hooks.h:
// SessionLive local pawn OR engine in-game (OR of signals)
// SessionFeaturesOk live (no extra timer)
// SessionEntityReady local pawn + engine in-game. NO write probes. ESP.
// SessionEntityOk EntityReady + alive write-ready
// (services/scene/origin/weapon) + not cinematic. Glow / world / combat.
// SessionMapLeaving true while leave wipe in flight (cross-thread)
// SessionOnMapLeave debounced wipe (LevelShutdown / FSN leave edge)
// SessionWatchLocalLife death/respawn edge: wipe caches holding free'd pawn*
// Per-(thread, frame) caching via g_presentFrame - repeated probes per frame
// short-circuit; cross-thread callers get their own TLS slot.
// ============================================================================
namespace {
    constexpr std::uint32_t kLeaveCoalesceMs = 400;

    std::atomic<std::uint64_t> g_leaveStartedMs{0};
    std::atomic<bool> g_sessionParked{false};
    std::atomic<bool> g_hadLocalAlive{false};
    std::atomic<bool> g_hadEngineInGame{false};
    std::atomic<bool> g_leaveGameWipePending{false};
    std::atomic<bool> g_deathWipePending{false};

    inline void SessionParkNow() noexcept {
        g_sessionParked.store(true, std::memory_order_relaxed);
        g_leaveStartedMs.store(static_cast<std::uint64_t>(::GetTickCount64()), std::memory_order_relaxed);
    }

    inline void SessionUnpark() noexcept {
        g_sessionParked.store(false, std::memory_order_relaxed);
        g_leaveStartedMs.store(0, std::memory_order_relaxed);
    }

    struct SessionFrameCache {
        std::uint32_t frame = 0xFFFFFFFFu;
        bool live = false;
        bool featuresOk = false;
        bool entityOk = false;
    };

    // Packed atomic slot - (frame << 5) | postMatch<<4 | entityReady<<3 |
    // live<<2 | featuresOk<<1 | entityOk. Shared (not TLS): values are
    // thread-independent. Manual-map has no TLS directory.
    static std::atomic<std::uint64_t> g_sessionCache{0};

    inline void SessionInvalidateCache() noexcept {
        g_sessionCache.store(0, std::memory_order_relaxed);
    }

    inline std::uint64_t SessionNowMs() noexcept {
        return static_cast<std::uint64_t>(::GetTickCount64());
    }

    inline bool SessionEngineInGame() noexcept {
        __try {
            // NGC+0x230: 2=CONNECTED (still loading), 6=FULL. Features at signon 2
            // walk half-init pawn/weapon/particles -> join hitch then crash.
            // No EngineClient fallback: after leave NGC is gone while in_game()
            // stays true -> Present recaches a dead entity list, then join AVs.
            if (!Engine2::NetworkGameClient())
                return false;
            return Engine2::SignonState() >= 6;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            TW_SEH_CATCH("SessionEngineInGame");
        }
        return false;
    }

    // Scene + origin only. Active-weapon wait delayed world/skins until first gun.
    static bool LocalPawnSceneReady(C_CSPlayerPawn* p) noexcept {
        if (!p || !Mem::ValidEntity(p))
            return false;
        CCSPlayer_WeaponServices* ws = nullptr;
        void* move = nullptr;
        CGameSceneNode* node = nullptr;
        __try {
            ws = p->GetWeaponServices();
            move = p->m_pMovementServices();
            node = p->m_pGameSceneNode();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
        if (!ws || !Mem::IsUserPtr(ws))
            return false;
        if (!move || !Mem::IsUserPtr(move))
            return false;
        if (!node || !Mem::IsUserPtr(node))
            return false;
        Vector_t pos{};
        __try {
            pos = node->m_vecAbsOrigin();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
        return std::isfinite(pos.x) && std::isfinite(pos.y) && std::isfinite(pos.z);
    }

    // Skip writes until pawn can take them: weapon_services +
    // game_scene_node + finite origin + live active weapon. TDM first-alive
    // ticks have services/scene but no gun - CreateMove AV there.
    static bool LocalPawnReady(C_CSPlayerPawn* p) noexcept {
        if (!LocalPawnSceneReady(p))
            return false;
        C_CSWeaponBase* wpn = nullptr;
        __try {
            wpn = p->GetActiveWeapon();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
        if (!wpn || !Mem::ValidEntity(wpn))
            return false;
        CGameSceneNode* wnode = nullptr;
        __try {
            wnode = ((C_BaseEntity*)wpn)->m_pGameSceneNode();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
        return wnode && Mem::IsUserPtr(wnode);
    }

    // Team intro / post-match cinematic. Skip all writes here.
    // Join+~10s crash = intro ends, pawn recycles, timer already expired.
    static bool SessionInCinematic() noexcept {
        void* rules = SdkPrioA::GameRules();
        if (!rules || !Mem::IsReadable(rules, 0xF08))
            return false;
        bool intro = false;
        int phase = 0;
        __try {
            intro = *reinterpret_cast<bool*>(reinterpret_cast<std::uint8_t*>(rules) + 0xF04);
            phase = *reinterpret_cast<int*>(reinterpret_cast<std::uint8_t*>(rules) + 0x84);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
        return intro || phase >= 4;
    }

    // Returns the current packed cache value - recomputes + stores when the
    // present frame advanced since the last probe.
    static bool SessionProbePostMatch(bool engineInGame) noexcept {
        if (!engineInGame)
            return false;
        void* rules = SdkPrioA::GameRules();
        if (!rules || !Mem::IsReadable(rules, 0x88))
            return false;
        int phase = 0;
        __try {
            phase = *reinterpret_cast<int*>(reinterpret_cast<std::uint8_t*>(rules) + 0x84);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
        // 5 = GAMEPHASE_MATCH_ENDED. 4 is HALFTIME - still a live round.
        return phase >= 5;
    }

    static std::uint64_t SessionRefreshCache() noexcept {
        const std::uint32_t frame = H::g_presentFrame.load(std::memory_order_relaxed);
        std::uint64_t v = g_sessionCache.load(std::memory_order_relaxed);
        if ((std::uint32_t)(v >> 5) == frame)
            return v;
        C_CSPlayerPawn* pawn = H::SafeLocalPlayer();
        const bool local = pawn != nullptr;
        const bool engine = SessionEngineInGame();
        const bool live = local || engine;
        const bool featuresOk = live;
        // Visuals gate: signon>=6 only. Local pawn dies/recycles in TDM -
        // requiring it wiped ESP. Writes stay on EntityOk.
        const bool entityReady = engine;
        // Write gate: alive + services/scene/origin. Team intro and "has a gun"
        // used to stall ESP/world/skins for seconds after join.
        C_CSPlayerPawn* alive = H::SafeLocalAlive();
        const bool writeReady = alive && LocalPawnSceneReady(alive);
        const bool entityOk = entityReady && writeReady;
        const bool postMatch = SessionProbePostMatch(engine);
        v = ((std::uint64_t)frame << 5) | (postMatch ? 16ull : 0ull) | (entityReady ? 8ull : 0ull) |
            (live ? 4ull : 0ull) | (featuresOk ? 2ull : 0ull) | (entityOk ? 1ull : 0ull);
        g_sessionCache.store(v, std::memory_order_relaxed);
        return v;
    }
} // namespace

bool H::SessionLive() noexcept {
    __try {
        return (SessionRefreshCache() & 4ull) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool H::SessionFeaturesOk() noexcept {
    __try {
        return (SessionRefreshCache() & 2ull) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool H::SessionEntityOk() noexcept {
    __try {
        return (SessionRefreshCache() & 1ull) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool H::SessionEntityReady() noexcept {
    __try {
        return (SessionRefreshCache() & 8ull) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool H::SessionMapLeaving() noexcept {
    // Sticky until next spawned pawn - not a 2.5s timer. Timer expiry let
    // ESP/glow walk entities during the next-queue heartbeat.
    return g_sessionParked.load(std::memory_order_relaxed);
}

bool H::SessionPostMatch() noexcept {
    __try {
        return (SessionRefreshCache() & 16ull) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void H::SessionDrainDeath() noexcept {
    if (!g_deathWipePending.exchange(false, std::memory_order_acq_rel))
        return;
    __try {
        Legitbot::Reset();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        TW_SEH_CATCH("Death.legitbotReset");
    }
    __try {
        Rage::Reset();
        Rage::ResetMelee();
        Velocity::Reset();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    __try {
        Pred::Invalidate();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        TW_SEH_CATCH("Death.predInvalidate");
    }
    __try {
        Bones::Invalidate();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        TW_SEH_CATCH("Death.bonesInvalidate");
    }
}

void H::SessionDrainGameLeave() noexcept {
    if (!g_leaveGameWipePending.exchange(false, std::memory_order_acq_rel))
        return;
    chams::ForgetSnapshotChams();
    __try {
        Legitbot::Reset();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        TW_SEH_CATCH("Leave.legitbotReset");
    }
    __try {
        Rage::Reset();
        Rage::ResetMelee();
        Velocity::Reset();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    __try {
        Pred::Invalidate();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        TW_SEH_CATCH("Leave.predInvalidate");
    }
    __try {
        Bones::Invalidate();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        TW_SEH_CATCH("Leave.bonesInvalidate");
    }
    __try {
        ScoreboardWeapons::OnLevelChange();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        TW_SEH_CATCH("SessionMapLeave.Scoreboard");
    }
    __try {
        Panorama::OnLevelChange();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        TW_SEH_CATCH("SessionMapLeave.Panorama");
    }
}

void H::SessionOnMapLeave() noexcept {
    SessionParkNow();
    g_hadLocalAlive.store(false, std::memory_order_relaxed);
    g_leaveGameWipePending.store(true, std::memory_order_release);
    SessionInvalidateCache();
    __try {
        SdkPrioA::OnLevelShutdown();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Con::Seh("SessionOnMapLeave", GetExceptionCode());
    }
    // Game-thread owned state (CreateMove/FSN): reset here, NOT on the render
    // thread. Render-owned caches are wiped by SdkPrioA::FlushRenderWipe in
    // hkPresent (level-shutdown/leave would race Present mid-draw otherwise).
    SessionDrainGameLeave();
}

void H::SessionWatchLocalLife() noexcept {
    __try {
        // Single probe per frame - the parked-branch re-probe below cost a
        // second SEH pawn chain every Present for a result one frame stale.
        C_CSPlayerPawn* pawn = SafeLocalAlive();
        const bool alive = pawn != nullptr;
        const bool wasAlive = g_hadLocalAlive.exchange(alive, std::memory_order_relaxed);
        const bool engine = SessionEngineInGame();
        const bool wasEngine = g_hadEngineInGame.exchange(engine, std::memory_order_relaxed);
        const std::uint64_t now = SessionNowMs();

        // Do NOT unpark on signon>=6. That is the 2nd-queue heartbeat window
        // (mmqueue registering/heartbeating). Wait for a real spawned pawn.
        if (g_sessionParked.load(std::memory_order_relaxed) && engine && alive) {
            if (LocalPawnSceneReady(pawn)) {
                const std::uint64_t start = g_leaveStartedMs.load(std::memory_order_relaxed);
                if (start == 0 || (now - start) >= kLeaveCoalesceMs) {
                    SessionUnpark();
                    SessionInvalidateCache();
                }
                return;
            }
        }

        if (alive) {
            return;
        }
        if (!engine && wasEngine) {
            // Present thread - only timestamps + render wipe flag.
            // Full SessionOnMapLeave (Aim/Pred) is game-thread
            // (hkLevelShutdown / FSN drain). Calling it here races CreateMove.
            SessionParkNow();
            g_hadLocalAlive.store(false, std::memory_order_relaxed);
            g_leaveGameWipePending.store(true, std::memory_order_release);
            SessionInvalidateCache();
            __try {
                SdkPrioA::RequestRenderWipe();
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                TW_SEH_CATCH("Life.engineLeaveWipe");
            }
            return;
        }
        if (wasAlive) {
            // Death: session flags only on Present. Aim/Pred/Bones live on
            // CreateMove - wiping them here UAF on the next hop/shot.
            SessionInvalidateCache();
            g_deathWipePending.store(true, std::memory_order_release);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        TW_SEH_CATCH("SessionWatchLife.outer");
    }
}

void H::Hooks::WaitForQuiescence(uint32_t ms) noexcept {
    const ULONGLONG deadline = GetTickCount64() + ms;
    while (s_hookDepth.load(std::memory_order_acquire) > 0) {
        if (GetTickCount64() >= deadline)
            break;
        ::Sleep(1);
    }
}

void H::Hooks::shutdown() {
    // NOT a permanent latch: a failed-init retry calls shutdown() then init()
    // again - if this stuck at true, the later F4 unload would skip teardown
    // with every hook still patched. init() resets it so each install cycle
    // gets exactly one teardown.
    if (s_down.exchange(true, std::memory_order_acq_rel))
        return;
    s_unloading.store(true, std::memory_order_release);
    HitLog::Shutdown();
    Legitbot::RemoveEvents();
    // Restore first so original bytes are back while trampolines stay valid
    // for in-flight GetOriginal(); Remove after a short drain.
    auto forEachHook = [](auto&& fn) {
        fn(IsRelativeMouseMode);
        fn(MouseInputEnabled);
        fn(SortPrimitives);
        fn(DrawSkyboxArray);
        fn(DrawAggregateSceneObjectArray);
        fn(GeneratePrimitives);
        fn(GenerateAnimatablePrimitives);
        fn(GlobalLightUpdate);
        fn(UpdateLightObject);
        fn(FrameStageNotify);
        fn(GetRenderFov);
        fn(GetViewModelOffsets);
        fn(OverrideView);
        fn(SetupFog);
        fn(GetScreenAspectRatio);
        fn(DrawScopeOverlay);
        fn(DrawCrosshair);
        fn(RenderFlashBangOverlay);
        fn(DrawLegs);
        fn(DrawSmokeVertex);
        fn(DrawSmokeArray);
        fn(RenderDecals);
        fn(CacheParticleEffect);
        fn(ParticleDrawArray);
        fn(CreateMove);
        fn(HandleViewAngles);
        fn(DrawGlow);
        fn(GetGlowColor);
        fn(ManageGlowSceneObject);
        fn(ApplyGlowScene);
        fn(FireEventClientSide);
        fn(AntiTamper);
        fn(OnAddEntity);
        fn(OnRemoveEntity);
        fn(LevelShutdown);
    };

    DisableVelocityMovementHook();
    DisableViewmodelRotationHook();
    Velocity::Shutdown();

    forEachHook([](auto& h) {
        __try {
            h.Restore();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    });
    // Every entry is disabled. No new detour can enter; an in-flight native
    // call must return before its trampoline or the DLL is released.
    while (s_hookDepth.load(std::memory_order_acquire) > 0)
        ::Sleep(1);
    HitLog::ReleaseAcknowledgements();
    chams::ForgetSnapshotChams();
    Velocity::Reset();
    forEachHook([](auto& h) {
        __try {
            h.Remove();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    });

    RemoveVelocityMovementHook();
    RemoveViewmodelRotationHook();

    __try {
        Panorama::Uninstall();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    __try {
        LocalRemovals::Restore();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    __try {
        RadarReveal::Restore();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }

    g_leaveStartedMs.store(0, std::memory_order_relaxed);
    Con::Tag("unload", Con::Level::Ok, "hooks down");
}
