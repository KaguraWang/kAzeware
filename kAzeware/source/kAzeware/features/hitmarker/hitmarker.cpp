#include "hitmarker.h"

#include "../../config/config.h"
#include "../../hooks/hooks.h"
#include "../../interfaces/interfaces.h"
#include "../../utils/console/console.h"
#include "../../utils/memory/patternscan/patternscan.h"
#include "../../utils/math/vector/vector.h"
#include "../bones/bones.h"
#include "../bullet_impact/bullet_impact.h"
#include "../hitsound/hitsound.h"
#include "../../../cs2/entity/C_CSPlayerPawn/C_CSPlayerPawn.h"
#include "../../../cs2/entity/CCSPlayerController/CCSPlayerController.h"
#include "../../../cs2/entity/handle.h"
#include "../../../cs2/sdk/IGameEvent.h"
#include "../w2s/w2s.h"
#include "../../../../external/imgui/imgui.h"

#include <Windows.h>
#include <cmath>
#include <cfloat>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <cstdint>

namespace {
    constexpr int kMaxMarks = 24;
    constexpr int kMaxFloats = 32;
    constexpr float kScreenLife = 0.42f;
    constexpr float kWorldLife = 1.15f;

    struct Mark {
        bool active = false;
        bool kill = false;
        bool head = false;
        float born = 0.f;
        float life = kScreenLife;
        float size = 12.f;
        Vector_t worldPos{};
        int damage = 0;
    };

    struct FloatDmg {
        bool active = false;
        bool kill = false;
        bool head = false;
        float born = 0.f;
        float life = 1.f;
        Vector_t worldPos{};
        ImVec2 offset{};
        int damage = 0;
    };

    Mark g_marks[kMaxMarks]{};
    int g_write = 0;
    FloatDmg g_floats[kMaxFloats]{};
    int g_floatWrite = 0;
    int g_floatSeed = 0;

    float g_screenBorn = -1.f;
    float g_screenLife = kScreenLife;
    bool g_screenKill = false;
    bool g_screenHead = false;
    int g_screenDmg = 0;

    struct PendingWorld {
        bool active = false;
        std::uint32_t pawnHandle = 0;
        int hitgroup = 0;
        int damage = 0;
        bool kill = false;
        bool head = false;
        float queuedAt = 0.f;
    };

    constexpr int kMaxPending = 8;
    PendingWorld g_pending[kMaxPending]{};
    int g_pendingWrite = 0;

    // Prevents double marks when player_death fires after player_hurt(kill=true)
    // for the same shot.
    struct RecentVictimMark {
        std::uint32_t pawnHandle = 0;
        float time = 0.f;
    };

    constexpr int kMaxRecentMarks = 8;
    RecentVictimMark g_recentMarks[kMaxRecentMarks]{};
    int g_recentMarkWrite = 0;
    static SRWLOCK g_hitLock = SRWLOCK_INIT;

    void NoteVictimMarked(std::uint32_t pawnHandle) {
        if (!pawnHandle)
            return;
        RecentVictimMark& r = g_recentMarks[g_recentMarkWrite];
        g_recentMarkWrite = (g_recentMarkWrite + 1) % kMaxRecentMarks;
        r.pawnHandle = pawnHandle;
        r.time = static_cast<float>(ImGui::GetTime());
    }

    bool WasVictimMarked(std::uint32_t pawnHandle, float maxAge) {
        if (!pawnHandle)
            return false;
        const float now = static_cast<float>(ImGui::GetTime());
        for (int i = 0; i < kMaxRecentMarks; ++i) {
            if (g_recentMarks[i].pawnHandle == pawnHandle && (now - g_recentMarks[i].time) <= maxAge)
                return true;
        }
        return false;
    }

    float Now() {
        return static_cast<float>(ImGui::GetTime());
    }

    CCSPlayerController* LocalController() {
        C_CSPlayerPawn* local = H::SafeLocalPlayer();
        if (!local)
            return nullptr;
        if (!I::GameEntity || !I::GameEntity->Instance)
            return nullptr;
        __try {
            CBaseHandle hCtrl = local->m_hController();
            if (hCtrl.valid())
                return I::GameEntity->Instance->Get<CCSPlayerController>(hCtrl);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        return nullptr;
    }

    std::uint32_t PawnHandleFromController(CCSPlayerController* ctrl) {
        if (!ctrl || !Mem::ValidEntity(ctrl))
            return 0;
        __try {
            CBaseHandle hPawn = ctrl->m_hPlayerPawn();
            return hPawn.valid() ? hPawn.raw() : 0;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return 0;
        }
    }

    C_CSPlayerPawn* PawnFromHandleRaw(std::uint32_t ph) {
        if (!ph || !I::GameEntity || !I::GameEntity->Instance)
            return nullptr;
        CBaseHandle h(ph & ENT_ENTRY_MASK, ph >> NUM_SERIAL_NUM_SHIFT_BITS);
        return I::GameEntity->Instance->Get<C_CSPlayerPawn>(h);
    }

    void PushWorld(const Vector_t& pos, bool kill, bool head, int dmg) {
        Mark& m = g_marks[g_write];
        g_write = (g_write + 1) % kMaxMarks;
        m.active = true;
        m.kill = kill;
        m.head = head;
        m.born = Now();
        m.life = kWorldLife * std::clamp(Config::hitmarker_duration, 0.25f, 2.5f);
        m.size = Config::hitmarker_world_size;
        m.worldPos = pos;
        m.damage = dmg;
    }

    void PushFloatDamage(const Vector_t& pos, bool kill, bool head, int dmg) {
        if (!Config::float_damage || dmg <= 0)
            return;
        FloatDmg& f = g_floats[g_floatWrite];
        g_floatWrite = (g_floatWrite + 1) % kMaxFloats;
        f.active = true;
        f.kill = kill;
        f.head = head;
        f.born = Now();
        f.life = std::clamp(Config::float_damage_duration, 0.3f, 3.f);
        f.worldPos = pos;

        // Stagger in screen pixels so nearby players cannot take the label's anchor.
        const float ang = (g_floatSeed % 64) * 2.39996f;
        const float radius = 4.f + (g_floatSeed % 3) * 3.f;
        f.offset = {std::cos(ang) * radius, -8.f + std::sin(ang) * radius};
        g_floatSeed = (g_floatSeed + 1) % 192;

        f.damage = dmg;
    }

    void PulseScreen(bool kill, bool head, int dmg) {
        g_screenBorn = Now();
        g_screenLife = kScreenLife * std::clamp(Config::hitmarker_duration, 0.25f, 2.5f);
        g_screenKill = kill;
        g_screenHead = head;
        g_screenDmg = dmg;
    }

    // The damage event names the victim. World impacts can belong to cover,
    // another victim, or another shot, so resolve only this pawn's pose.
    bool ResolveVictimPos(C_CSPlayerPawn* pawn, int hitgroup, Vector_t& out) {
        if (!pawn || !Mem::ValidEntity(pawn))
            return false;

        const int group = Bones::HitgroupToHitbox(hitgroup);
        const int hitbox = group >= 0 ? group : Config::HB_CHEST;
        Vector_t origin{};
        const bool haveOrigin = Bones::GetOrigin(pawn, origin);
        Vector_t point{};
        __try {
            if (Bones::GetHitboxPoint(pawn, hitbox, point) && Bones::IsValidPos(point) &&
                (!haveOrigin || (point - origin).Length() <= 96.f)) {
                out = point;
                return true;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }

        if (!haveOrigin)
            return false;
        const Vector_t eye = Bones::GetEyePos(pawn);
        const float height = std::clamp(eye.z - origin.z, 40.f, 64.f);
        const float fraction = hitgroup == 1 ? 1.f : (hitgroup == 6 || hitgroup == 7) ? 0.3f : 0.8f;
        origin.z += height * fraction;
        out = origin;
        return true;
    }

    // Caller holds g_hitLock so both visual effects publish the same anchor.
    void EmitWorldMark(const Vector_t& pos, bool kill, bool head, int dmg) {
        if (Config::hitmarker && Config::hitmarker_world)
            PushWorld(pos, kill, head, dmg);
        PushFloatDamage(pos, kill, head, dmg);
    }

    void QueueWorld(std::uint32_t pawnHandle, int hitgroup, int dmg, bool kill, bool head) {
        if (!pawnHandle)
            return;

        Vector_t anchor{};
        const bool resolved = ResolveVictimPos(PawnFromHandleRaw(pawnHandle), hitgroup, anchor);
        AcquireSRWLockExclusive(&g_hitLock);
        if (resolved) {
            EmitWorldMark(anchor, kill, head, dmg);
        } else if (!kill) {
            g_pending[g_pendingWrite] = {true, pawnHandle, hitgroup, dmg, kill, head, Now()};
            g_pendingWrite = (g_pendingWrite + 1) % kMaxPending;
        }
        ReleaseSRWLockExclusive(&g_hitLock);
    }

    void DrainPendingWorld() {
        const float now = Now();
        AcquireSRWLockExclusive(&g_hitLock);
        for (auto& pending : g_pending) {
            if (!pending.active)
                continue;
            if (now - pending.queuedAt >= 1.f) {
                pending.active = false;
                continue;
            }
            Vector_t anchor{};
            if (ResolveVictimPos(PawnFromHandleRaw(pending.pawnHandle), pending.hitgroup, anchor)) {
                EmitWorldMark(anchor, pending.kill, pending.head, pending.damage);
                pending.active = false;
            }
        }
        ReleaseSRWLockExclusive(&g_hitLock);
    }

    ImU32 ColorFor(bool kill, bool head, float alpha) {
        ImVec4 c = Config::hitmarker_color;
        if (kill)
            c = Config::hitmarker_kill_color;
        else if (head)
            c = Config::hitmarker_head_color;
        c.w *= alpha;
        return ImGui::ColorConvertFloat4ToU32(c);
    }

    // Hit markers: 4 diagonal arms, alpha gradient
    // 0 at the outer tip -> full toward the center, spawn expand-in (quartic
    // ease-out, ~50 ms), ease-out fade (1-progress?). `t` = age seconds.
    void DrawVelArms(ImDrawList* dl, ImVec2 c, float t, float life, float sizeMul, ImU32 col, float thickness) {
        if (!dl || sizeMul <= 0.f)
            return;
        const float progress = life > 0.f ? std::clamp(t / life, 0.f, 1.f) : 1.f;
        const float easeOut = 1.f - progress * progress;

        const float expandProgress = (std::min)(t * 20.f, 1.f);
        const float easeExpand = 1.f - std::pow(1.f - expandProgress, 4.f);
        const float expandAmount = 6.f * (1.f - easeExpand);

        const float scale = sizeMul / 10.f;
        const float arm = (10.f + expandAmount) * scale;
        const float gap = (3.f + expandAmount * 0.3f) * scale;

        const float g = gap * 0.70710678f;
        const float a = arm * 0.70710678f;
        const int colA = (int)((col >> 24) & 0xFF);

        auto armSeg = [&](ImVec2 dir) {
            const ImVec2 pIn(c.x + dir.x * g, c.y + dir.y * g);
            const ImVec2 pOut(c.x + dir.x * a, c.y + dir.y * a);
            // tip (t=1) alpha 0 -> center (t=0) alpha colA*easeOut
            constexpr int kSeg = 6;
            for (int i = 0; i < kSeg; ++i) {
                const float t0 = static_cast<float>(i) / kSeg;
                const float t1 = static_cast<float>(i + 1) / kSeg;
                const float a0 = 1.f - t0;
                const float a1 = 1.f - t1;
                const ImU32 c0 = (col & 0x00FFFFFFu) | (static_cast<std::uint32_t>(colA * easeOut * a0) << 24);
                const ImU32 c1 = (col & 0x00FFFFFFu) | (static_cast<std::uint32_t>(colA * easeOut * a1) << 24);
                const ImVec2 q0(pIn.x + (pOut.x - pIn.x) * t0, pIn.y + (pOut.y - pIn.y) * t0);
                const ImVec2 q1(pIn.x + (pOut.x - pIn.x) * t1, pIn.y + (pOut.y - pIn.y) * t1);
                dl->AddLine(q0, q1, c0, thickness);
                (void)c1;
            }
        };

        armSeg(ImVec2(1.f, 1.f));
        armSeg(ImVec2(-1.f, -1.f));
        armSeg(ImVec2(1.f, -1.f));
        armSeg(ImVec2(-1.f, 1.f));
    }

    // Damage label: above the marker (20 px), sin/cos shake that dies
    // within ~125 ms, fades with the marker.
    void DrawDamageLabel(ImDrawList* dl, ImVec2 c, float t, int dmg, ImU32 col, float easeOut) {
        if (!dl || dmg <= 0)
            return;
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", dmg);
        const ImVec2 ts = ImGui::CalcTextSize(buf);
        const float shakeProg = (std::max)(0.f, 1.f - t * 8.f);
        const float sx = std::sin(t * 50.f) * shakeProg * 3.f;
        const float sy = std::cos(t * 45.f) * shakeProg * 2.f;
        const ImVec2 tp(c.x - ts.x * 0.5f + sx, c.y - 20.f - ts.y + sy);
        const int a = (int)(((col >> 24) & 0xFF) * easeOut);
        const ImU32 shadow = IM_COL32(0, 0, 0, (int)(200.f * easeOut));
        dl->AddText(ImVec2(tp.x + 1.f, tp.y + 1.f), shadow, buf);
        dl->AddText(tp, col, buf);
        (void)a;
    }

    void DrawScreenMarker(ImDrawList* dl) {
        if (!Config::hitmarker || !Config::hitmarker_screen)
            return;
        if (g_screenBorn < 0.f)
            return;

        const float t = Now() - g_screenBorn;
        if (t >= g_screenLife) {
            g_screenBorn = -1.f;
            return;
        }

        const ImGuiIO& io = ImGui::GetIO();
        const ImVec2 c(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
        const ImU32 col = ColorFor(g_screenKill, g_screenHead, 1.f);
        DrawVelArms(dl, c, t, g_screenLife, Config::hitmarker_size, col, Config::hitmarker_thickness);

        if (Config::hitmarker_show_damage && g_screenDmg > 0)
            DrawDamageLabel(dl, c, t, g_screenDmg, col, 1.f - (t / g_screenLife) * (t / g_screenLife));
    }

    void DrawWorldMarkers(ImDrawList* dl, const ViewMatrix& vm) {
        if (!Config::hitmarker || !Config::hitmarker_world)
            return;
        if (!dl)
            return;

        const float now = Now();
        Mark marks[kMaxMarks];
        AcquireSRWLockShared(&g_hitLock);
        std::memcpy(marks, g_marks, sizeof(marks));
        ReleaseSRWLockShared(&g_hitLock);
        for (const auto& m : marks) {
            const float t = now - m.born;
            if (!m.active || t < 0.f || t >= m.life)
                continue;

            Vector_t scr{};
            if (!vm.WorldToScreen(m.worldPos, scr))
                continue;

            const ImVec2 c(scr.x, scr.y);
            const ImU32 col = ColorFor(m.kill, m.head, 1.f);
            DrawVelArms(dl, c, t, m.life, m.size, col, Config::hitmarker_thickness);

            if (Config::hitmarker_show_damage && m.damage > 0) {
                const float easeOut = 1.f - (t / m.life) * (t / m.life);
                DrawDamageLabel(dl, c, t, m.damage, col, easeOut);
            }
        }
    }

    void DrawFloatingDamage(ImDrawList* dl, const ViewMatrix& vm) {
        if (!Config::float_damage || !dl)
            return;
        const float now = Now();
        const float speed = std::clamp(Config::float_damage_speed, 10.f, 200.f);
        FloatDmg floats[kMaxFloats];
        AcquireSRWLockShared(&g_hitLock);
        std::memcpy(floats, g_floats, sizeof(floats));
        ReleaseSRWLockShared(&g_hitLock);
        for (const auto& f : floats) {
            const float t = now - f.born;
            if (!f.active || t < 0.f || t >= f.life)
                continue;
            Vector_t scr{};
            if (!vm.WorldToScreen(f.worldPos, scr))
                continue;
            const float u = t / f.life;
            const float fade = (u > 0.55f) ? (1.f - (u - 0.55f) / 0.45f) : 1.f;
            const float alpha = std::clamp(fade, 0.f, 1.f);

            // Bouncing physics with gravity
            const float speedUp = speed * 1.8f;
            float zOffset = speedUp * t - 0.5f * 600.f * t * t; // v0*t - 0.5*g*t^2
            if (zOffset < 0.f && t > 0.1f) {
                // bounce once
                const float bounceTime = t - (speedUp * 2.f / 600.f); // time since first bounce
                zOffset = (std::max)(0.f, (speedUp * 0.4f) * bounceTime - 0.5f * 600.f * bounceTime * bounceTime);
            }

            const float rise = zOffset;
            const float pop = std::sin((std::min)(u * 12.f, 3.14159f)) * 0.5f;
            const float scale = 1.f + pop;

            ImVec4 cv = Config::float_damage_color;
            if (f.kill)
                cv = Config::float_damage_kill_color;
            else if (f.head)
                cv = Config::float_damage_head_color;
            cv.w *= alpha;
            const ImU32 col = ImGui::ColorConvertFloat4ToU32(cv);

            char buf[16];
            snprintf(buf, sizeof(buf), "%d", f.damage);
            ImFont* font = ImGui::GetFont();
            const float fs = ImGui::GetFontSize() * scale;
            const ImVec2 ts = font ? font->CalcTextSizeA(fs, FLT_MAX, 0.f, buf) : ImGui::CalcTextSize(buf);
            const ImVec2 tp(scr.x + f.offset.x - ts.x * 0.5f, scr.y + f.offset.y - rise - ts.y * 0.5f);
            const ImU32 shadow = IM_COL32(0, 0, 0, static_cast<int>(210.f * alpha));
            if (font) {
                dl->AddText(font, fs, ImVec2(tp.x + 1.f, tp.y + 1.f), shadow, buf);
                dl->AddText(font, fs, tp, col, buf);
            } else {
                dl->AddText(ImVec2(tp.x + 1.f, tp.y + 1.f), shadow, buf);
                dl->AddText(tp, col, buf);
            }
        }
    }

    void HandleHurt(IGameEvent* ev) {
        CCSPlayerController* localCtrl = LocalController();
        if (!localCtrl)
            return;

        CCSPlayerController* attacker = ev->GetPlayerController("attacker");
        if (!attacker || attacker != localCtrl)
            return;

        CCSPlayerController* victim = ev->GetPlayerController("userid");
        if (victim && victim == localCtrl)
            return;

        const int dmg = static_cast<int>(ev->GetInt64("dmg_health"));
        const int hitgroup = static_cast<int>(ev->GetInt64("hitgroup"));
        const int health = static_cast<int>(ev->GetInt64("health"));
        if (dmg <= 0)
            return;

        const bool head = (hitgroup == 1);
        const bool kill = (health <= 0);

        if (Config::hitsound)
            Hitsound::Play(head, kill);

        if (Config::hitmarker && Config::hitmarker_screen)
            PulseScreen(kill, head, dmg);

        const std::uint32_t pawnHandle = PawnHandleFromController(victim);
        if (kill)
            NoteVictimMarked(pawnHandle);
        if ((Config::hitmarker && Config::hitmarker_world) || Config::float_damage)
            QueueWorld(pawnHandle, hitgroup, dmg, kill, head);
    }

    void HandleDeath(IGameEvent* ev) {
        CCSPlayerController* localCtrl = LocalController();
        if (!localCtrl)
            return;

        CCSPlayerController* attacker = ev->GetPlayerController("attacker");
        if (!attacker || attacker != localCtrl)
            return;

        CCSPlayerController* victim = ev->GetPlayerController("userid");
        if (!victim || victim == localCtrl)
            return;

        // player_death has "headshot" (bool), not "hitgroup" - reading hitgroup
        // yields 0, so head-slot resolution collapsed to spine on kills.
        const bool head = ev->GetInt64("headshot", 0) != 0;
        const int hitgroup = head ? 1 : 0;

        // If player_hurt(kill=true) already fired for this victim (same shot),
        // its resolution used the still-alive pawn and is more accurate. Skip
        // the death-time re-resolve which would run on a ragdolling pawn.
        const std::uint32_t vh = PawnHandleFromController(victim);
        const bool alreadyMarked = WasVictimMarked(vh, 0.5f);

        if (Config::hitmarker && Config::hitmarker_screen && !alreadyMarked)
            PulseScreen(true, head, 0);

        if (alreadyMarked)
            return;

        if ((Config::hitmarker && Config::hitmarker_world) || Config::float_damage)
            QueueWorld(vh, hitgroup, 0, true, head);
    }

} // namespace

void Hitmarker::NoteLastFire(const Vector_t& eye, const QAngle_t& fireAngles) {
    BulletFx::NoteLastFire(eye, fireAngles);
}

void Hitmarker::Install() {
    IGameEvent::InitPatterns();
    g_screenBorn = -1.f;
    g_write = 0;
    g_floatWrite = 0;
    g_pendingWrite = 0;
    g_recentMarkWrite = 0;
    for (int i = 0; i < kMaxMarks; ++i)
        g_marks[i] = Mark{};
    for (int i = 0; i < kMaxFloats; ++i)
        g_floats[i] = FloatDmg{};
    for (int i = 0; i < kMaxPending; ++i)
        g_pending[i] = PendingWorld{};
    for (int i = 0; i < kMaxRecentMarks; ++i)
        g_recentMarks[i] = RecentVictimMark{};
}

uintptr_t Hitmarker::ReportHitAddr() {
    return 0;
}

void Hitmarker::Shutdown() {
    g_screenBorn = -1.f;
    for (int i = 0; i < kMaxMarks; ++i)
        g_marks[i].active = false;
    for (int i = 0; i < kMaxFloats; ++i)
        g_floats[i].active = false;
    for (int i = 0; i < kMaxPending; ++i)
        g_pending[i].active = false;
    for (int i = 0; i < kMaxRecentMarks; ++i)
        g_recentMarks[i].pawnHandle = 0;
}

void Hitmarker::OnGameEvent(void* gameEvent) {
    if (!gameEvent)
        return;
    if (!Config::hitmarker && !Config::hitsound && !Config::float_damage)
        return;

    IGameEvent* ev = static_cast<IGameEvent*>(gameEvent);
    const char* name = nullptr;
    __try {
        name = ev->GetName();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    if (!name || !name[0])
        return;

    __try {
        if (std::strcmp(name, "player_hurt") == 0)
            HandleHurt(ev);
        else if (std::strcmp(name, "player_death") == 0)
            HandleDeath(ev);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void Hitmarker::Draw(const ViewMatrix& vm) {
    if (!Config::hitmarker && !Config::float_damage)
        return;
    DrainPendingWorld();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    if (!dl)
        return;
    if (Config::hitmarker) {
        DrawWorldMarkers(dl, vm);
        DrawScreenMarker(dl);
    }
    DrawFloatingDamage(dl, vm);
}
