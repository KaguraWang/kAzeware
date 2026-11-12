#include "accuracy.h"
#include "semi_rage.h"
#include "ray.h"
#include "damage.h"
#include "legit_math.h"
#include "../../utils/memory/patternscan/patternscan.h"
#include "../../../cs2/entity/C_CSWeaponBase/C_CSWeaponBase.h"
#include "../../../cs2/entity/C_CSPlayerPawn/C_CSPlayerPawn.h"
#include "../../../cs2/entity/CCSPlayerController/CCSPlayerController.h"
#include "../../interfaces/interfaces.h"
#include "../../interfaces/CUserCmd/CUserCmd.h"
#include "../../utils/cvar/cvar.h"
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <array>
#include <bit>

namespace Legitbot {
    namespace {
        using InaccuracyFn = float(__fastcall*)(void*, float*, float*);
        using SpreadFn = float(__fastcall*)(void*);
        InaccuracyFn getInaccuracy = nullptr;
        SpreadFn getSpread = nullptr, getRecovery = nullptr;
        using SeedFn = std::uint32_t(__fastcall*)(void*, const QAngle_t*, int);
        using FillFn = void*(__fastcall*)(void*, void*, int);

        struct ShotTime {
            int tick;
            float fraction;
        };

        using MapTickFn = int*(__fastcall*)(int*, int, int);
        using EyeFn = Vector_t*(__fastcall*)(Vector_t*, void*, const ShotTime*, int, void*);
        using PunchFn = void(__fastcall*)(void*, QAngle_t*, void*, char);
        MapTickFn mapShotTick = nullptr;
        EyeFn eyeAtTime = nullptr;
        PunchFn punchAtTime = nullptr;
        std::uint32_t punchServicesOffset = 0;
        using PatternFindFn = int(__fastcall*)(void*, const std::uint16_t*, void*);
        SeedFn computeSeed = nullptr;
        FillFn fillFire = nullptr;
        PatternFindFn findSpreadPattern = nullptr;
        void* spreadPatterns = nullptr;
        std::uint32_t firePunchOffset = 0, fireCommandOffset = 0;
        std::uint32_t shotHistoryOffset = 0, shotHistoryHead = 0, shotHistoryCount = 0;
        using PushLoggingFn = void (*)(bool, bool);
        using PopLoggingFn = void (*)(bool);
        PushLoggingFn pushPreviewLogging = nullptr;
        PopLoggingFn popPreviewLogging = nullptr;
        constexpr float kTwoPi = 6.283185307179586f;

        struct ValveRng {
            int state = 0;
            int index = 0;
            int table[32]{};
            bool seeded = false;

            static int Lcg(int v) {
                return 16807 * (v % 127773) - 2836 * (v / 127773);
            }

            void Seed(int s) {
                state = s > 0 ? -s : s;
                if (state == INT_MIN)
                    state = -1;
                if (state == 0)
                    state = -1;
                index = 0;
                seeded = false;
            }

            int Generate() {
                if (!seeded) {
                    int v = -state;
                    if (v < 1)
                        v = 1;
                    for (int j = 39; j >= 0; --j) {
                        v = Lcg(v);
                        if (v < 0)
                            v += 2147483647;
                        if (j < 32)
                            table[j] = v;
                    }
                    state = v;
                    index = table[0];
                    seeded = true;
                }
                state = Lcg(state);
                if (state < 0)
                    state += 2147483647;
                const int idx = index / 0x4000000;
                index = table[idx];
                table[idx] = state;
                return index;
            }

            float RandomFloat(float lo, float hi) {
                const float norm = (std::min)(0.99999988f, static_cast<float>(Generate()) * 4.6566129e-10f);
                return lo + norm * (hi - lo);
            }
        };

        struct SpreadBasis {
            float inaccuracy, spread, ci, si, cs, ss;
        };

        const auto& SpreadTable() {
            static const auto table = []() {
                std::array<std::array<SpreadBasis, 16>, 256> result{};
                for (int seed = 0; seed < 256; seed++) {
                    ValveRng rng;
                    rng.Seed(seed + 1);
                    for (auto& p : result[seed]) {
                        p.inaccuracy = rng.RandomFloat(0, 1);
                        const float a = rng.RandomFloat(0, kTwoPi);
                        p.spread = rng.RandomFloat(0, 1);
                        const float b = rng.RandomFloat(0, kTwoPi);
                        p.ci = std::cos(a);
                        p.si = std::sin(a);
                        p.cs = std::cos(b);
                        p.ss = std::sin(b);
                    }
                }
                return result;
            }();
            return table;
        }

        float Reshape(float radius, float recoil, int definition, int mode) {
            if (definition == 64 && mode == 1)
                return 1.f - radius * radius;
            if (definition == 28 && recoil < 3) {
                int c = 3;
                do {
                    --c;
                    radius *= radius;
                } while (float(c) > recoil);
                return 1.f - radius;
            }
            return radius;
        }

        void CalcSpreadValve(int seed,
                             float inaccuracy,
                             float spread,
                             float recoil,
                             int definition,
                             int mode,
                             int bullets,
                             float* x,
                             float* y) {
            const auto& points = SpreadTable()[seed - 1];
            for (int i = 0; i < bullets; i++) {
                const auto& p = points[i];
                const float a = Reshape(p.inaccuracy, recoil, definition, mode) * inaccuracy,
                            b = Reshape(p.spread, recoil, definition, mode) * spread;
                x[i] = p.ci * a + p.cs * b;
                y[i] = p.si * a + p.ss * b;
            }
        }

    } // namespace

    bool InitAccuracy() {
        const auto tier0 = GetModuleHandleA("tier0.dll");
        pushPreviewLogging = reinterpret_cast<PushLoggingFn>(GetProcAddress(tier0, "LoggingSystem_PushLoggingState"));
        popPreviewLogging = reinterpret_cast<PopLoggingFn>(GetProcAddress(tier0, "LoggingSystem_PopLoggingState"));
        getInaccuracy = reinterpret_cast<InaccuracyFn>(
            M::FindPattern("client.dll", "48 89 5C 24 10 55 56 57 48 81 EC B0 00 00 00 44"));
        getSpread = reinterpret_cast<SpreadFn>(M::FindPattern(
            "client.dll",
            "48 63 91 ? ? ? ? 48 8B 81 ? ? ? ? 85 D2 78 10 48 83 FA 02 73 0A F3 0F 10 84 90 50 07 00 00 C3"));
        getRecovery = reinterpret_cast<SpreadFn>(
            M::FindPattern("client.dll", "48 89 5C 24 ? 57 48 83 EC 30 48 8B D9 E8 ? ? ? ? 48 8B F8 48 85 C0 75"));
        computeSeed = reinterpret_cast<SeedFn>(M::FindPattern(
            "client.dll",
            "48 89 5C 24 08 57 48 81 EC F0 00 00 00 F3 0F 10 0A 48 8D 8C 24 10 01 00 00 41 8B D8 48 8B FA E8"));
        fillFire = reinterpret_cast<FillFn>(
            M::FindPattern("client.dll", "48 8B C4 55 53 56 57 41 55 48 8D 68 98 48 81 EC 40 01 00 00"));
        if (auto* p = M::FindPattern("client.dll", "41 8B 84 24 ? ? ? ? 89 85 ? ? ? ? 8D 58 FF 85 DB"))
            shotHistoryCount = *reinterpret_cast<std::uint32_t*>(p + 4);
        if (auto* p = M::FindPattern("client.dll", "41 8B 8C 24 ? ? ? ? 03 CB 81 E1 1F 00 00 80"))
            shotHistoryHead = *reinterpret_cast<std::uint32_t*>(p + 4);
        if (auto* p = M::FindPattern("client.dll", "4C 6B F0 78 49 81 C6 ? ? ? ? 4D 03 F4"))
            shotHistoryOffset = *reinterpret_cast<std::uint32_t*>(p + 7);
        if (auto* p = M::FindPattern("client.dll", "41 8B 57 68 F3 41 0F 10 77 6C E8 ? ? ? ?"))
            mapShotTick = reinterpret_cast<MapTickFn>(M::GetAbsoluteAddress(p + 10, 1));
        if (auto* p = M::FindPattern("client.dll", "45 33 C9 49 8B D4 E8 ? ? ? ? 48 8D 5F 20"))
            eyeAtTime = reinterpret_cast<EyeFn>(M::GetAbsoluteAddress(p + 6, 1));
        if (auto* p = M::FindPattern("client.dll", "49 8B 8D ? ? ? ? 48 8D 54 24 30 41 B1 01 4C 8B C7 E8")) {
            punchServicesOffset = *reinterpret_cast<std::uint32_t*>(p + 3);
            punchAtTime = reinterpret_cast<PunchFn>(M::GetAbsoluteAddress(p + 18, 1));
        }
        if (auto* cache = M::FindPattern("client.dll", "F2 0F 11 4F 2C 89 47 34 F2 0F 11 8E ? ? ? ? 89 86 ? ? ? ? EB"))
            firePunchOffset = *reinterpret_cast<std::uint32_t*>(cache + 12);
        if (auto* command = M::FindPattern("client.dll", "4C 8B A8 ? ? ? ? 4D 85 ED 0F 84 ? ? ? ? 45 85 F6"))
            fireCommandOffset = *reinterpret_cast<std::uint32_t*>(command + 3);
        if (auto* pattern = M::FindPattern("client.dll", "48 8D 0D ? ? ? ? 45 03 CC E8 ? ? ? ? EB 26 41 0F 28 CA")) {
            spreadPatterns = M::GetAbsoluteAddress(pattern, 3);
            auto* helper = M::GetAbsoluteAddress(pattern + 10, 1);
            findSpreadPattern = reinterpret_cast<PatternFindFn>(M::GetAbsoluteAddress(helper + 0x25, 1));
        }
        Cvar::Bool("weapon_accuracy_shotgun_spread_patterns", true);
        Cvar::Bool("weapon_debug_max_inaccuracy", false);
        Cvar::Bool("weapon_debug_inaccuracy_only_up", false);
        return getInaccuracy && getSpread;
    }

    bool ReadSeedShot(CUserCmd* cmd, C_CSPlayerPawn* local, C_CSWeaponBase* weapon, SeedShot& shot) {
        return ReadSeedShotAt(cmd, local, weapon, -1, false, shot);
    }

    bool ReadShotClock(CUserCmd* cmd, C_CSWeaponBase* weapon, double& clock) {
        if (!cmd || !weapon || !mapShotTick)
            return false;
        const int count = cmd->csgoUserCmd.inputHistoryField.nCurrentSize;
        if (count <= 0 || count > 128)
            return false;
        const auto* entry = cmd->GetInputHistoryEntry(count - 1);
        if (!entry || entry->nPlayerTickCount <= 0 || !std::isfinite(entry->flPlayerTickFraction) ||
            entry->flPlayerTickFraction < 0.f || entry->flPlayerTickFraction >= 1.f)
            return false;
        __try {
            const auto identity = *reinterpret_cast<std::uintptr_t*>(reinterpret_cast<std::uintptr_t>(weapon) + 0x10);
            int mapped = 0;
            mapShotTick(&mapped, entry->nPlayerTickCount, *reinterpret_cast<int*>(identity + 0x38));
            if (mapped <= 0)
                return false;
            clock = double(mapped) + entry->flPlayerTickFraction;
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    bool ChargedShotTime(C_CSPlayerPawn* local, C_CSWeaponBase* weapon, int& historyTick, bool secondary) {
        if (!local || !weapon || !shotHistoryOffset || !shotHistoryHead || !shotHistoryCount)
            return false;
        const double time =
            std::floor(
                double(secondary ? weapon->m_nNextSecondaryAttackTick() : weapon->m_nNextPrimaryAttackTick()) +
                (secondary ? weapon->m_flNextSecondaryAttackTickRatio() : weapon->m_flNextPrimaryAttackTickRatio()) +
                weapon->m_flWatTickOffset()) -
            1.;
        if (!std::isfinite(time) || time <= 0 || time >= INT_MAX)
            return false;
        historyTick = int(time);
        __try {
            auto* services = reinterpret_cast<unsigned char*>(local->m_pWeaponServices());
            if (!services)
                return false;
            const int count = *reinterpret_cast<int*>(services + shotHistoryCount),
                      head = *reinterpret_cast<int*>(services + shotHistoryHead);
            if (count < 0 || count > 32 || head < 0 || head >= 32)
                return false;
            if (!count)
                return true;
            const auto* latest =
                reinterpret_cast<CCSGOInputHistoryEntryPB*>(services + shotHistoryOffset) + ((head + count - 1) % 32);
            // Equal/older samples do not replace the server's cached aim.
            return time > double(latest->nPlayerTickCount) + latest->flPlayerTickFraction;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    static bool ReadChargedSeedShot(CUserCmd* cmd,
                                    C_CSPlayerPawn* local,
                                    C_CSWeaponBase* weapon,
                                    int historyIndex,
                                    bool secondary,
                                    SeedShot& shot) {
        int tick = 0;
        if (!ChargedShotTime(local, weapon, tick, secondary) || !fillFire || !firePunchOffset || !fireCommandOffset ||
            !pushPreviewLogging || !popPreviewLogging || !I::GameEntity || !I::GameEntity->Instance)
            return false;
        const auto* entry = cmd ? cmd->GetInputHistoryEntry(historyIndex) : nullptr;
        if (!entry || !entry->pViewAngles)
            return false;
        auto* controller = I::GameEntity->Instance->Get<CCSPlayerController>(local->m_hController());
        if (!controller)
            return false;
        auto* services = reinterpret_cast<unsigned char*>(local->m_pWeaponServices());
        auto* ring = reinterpret_cast<CCSGOInputHistoryEntryPB*>(services + shotHistoryOffset);
        auto* current = reinterpret_cast<CUserCmd**>(reinterpret_cast<std::uintptr_t>(controller) + fireCommandOffset);
        auto* cache = reinterpret_cast<QAngle_t*>(reinterpret_cast<std::uintptr_t>(weapon) + firePunchOffset);

        struct Raw {
            int tick;
            float fraction;
            int unknown;
            int renderTick;
            float renderFraction;
            Vector_t eye;
            QAngle_t angle, punch;
            bool history;
            char flags[3];
            int source;
            char tail[64];
        } raw{};

        // ItemPostFrame consumes the newly appended history after movement. Preview
        // that entry without running movement, firing, or changing prediction slots.
        const auto saved = *ring;
        const int head = *reinterpret_cast<int*>(services + shotHistoryHead),
                  count = *reinterpret_cast<int*>(services + shotHistoryCount);
        const auto previous = *current;
        const auto punch = *cache;
        auto& attackIndex =
            secondary ? cmd->csgoUserCmd.nAttack2StartHistoryIndex : cmd->csgoUserCmd.nAttack1StartHistoryIndex;
        const int attack = attackIndex;
        bool filled = false;
        __try {
            *ring = *entry;
            ring->nPlayerTickCount = tick;
            ring->flPlayerTickFraction = 0.f;
            *reinterpret_cast<int*>(services + shotHistoryHead) = 0;
            *reinterpret_cast<int*>(services + shotHistoryCount) = 1;
            *current = cmd;
            attackIndex = -1;
            // A preview may request a time outside the native position ring. Native
            // fallback is valid, but its warning must not be printed for every aim
            // candidate. Scope listener removal to this call and this thread only.
            pushPreviewLogging(true, true);
            __try {
                fillFire(weapon, &raw, secondary ? 1 : 0);
            } __finally {
                popPreviewLogging(true);
            }
            filled = true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            filled = false;
        }
        *ring = saved;
        *reinterpret_cast<int*>(services + shotHistoryHead) = head;
        *reinterpret_cast<int*>(services + shotHistoryCount) = count;
        *current = previous;
        *cache = punch;
        attackIndex = attack;
        if (!filled || raw.history || raw.source != 2 || raw.tick <= 0 || !std::isfinite(raw.fraction) ||
            raw.fraction < 0 || raw.fraction >= 1 || !raw.angle.IsValid() || !Bones::IsValidPos(raw.eye))
            return false;
        shot = {raw.tick, raw.fraction, raw.eye, raw.angle, raw.punch};
        return true;
    }

    bool ReadSeedShotAt(CUserCmd* cmd,
                        C_CSPlayerPawn* local,
                        C_CSWeaponBase* weapon,
                        int historyIndex,
                        bool secondary,
                        SeedShot& shot,
                        bool charged) {
        if (charged) {
            shot = {};
            return ReadChargedSeedShot(cmd, local, weapon, historyIndex, secondary, shot);
        }
        shot = {};
        if (!cmd || !local || !weapon || !fillFire || !computeSeed || !firePunchOffset || !fireCommandOffset ||
            !mapShotTick || !eyeAtTime || !punchAtTime || !punchServicesOffset || !I::GameEntity ||
            !I::GameEntity->Instance)
            return false;
        auto* controller = I::GameEntity->Instance->Get<CCSPlayerController>(local->m_hController());
        if (!controller)
            return false;
        const int count = cmd->csgoUserCmd.inputHistoryField.nCurrentSize;
        if (historyIndex < 0)
            historyIndex = count - 1;
        if (count <= 0 || count > 128 || historyIndex >= count || !cmd->GetInputHistoryEntry(historyIndex))
            return false;
        const auto* entry = cmd->GetInputHistoryEntry(historyIndex);
        const ShotTime time{entry->nPlayerTickCount, entry->flPlayerTickFraction};
        if (time.tick <= 0 || !std::isfinite(time.fraction) || time.fraction < 0.f || time.fraction >= 1.f)
            return false;

        // FillGunFireData reads the controller's prediction command, which can be
        // absent or stale after CreateMove. Preview the command Fire() will submit.
        struct Raw {
            int tick;
            float fraction;
            int unknown;
            int renderTick;
            float renderFraction;
            Vector_t eye;
            QAngle_t angle, punch;
            bool history;
            char flags[3];
            int source;
            char tail[64];
        } raw{};

        static_assert(offsetof(Raw, eye) == 20 && offsetof(Raw, punch) == 44);
        QAngle_t saved{};
        auto& attackIndex =
            secondary ? cmd->csgoUserCmd.nAttack2StartHistoryIndex : cmd->csgoUserCmd.nAttack1StartHistoryIndex;
        const int index = attackIndex;
        auto* cache = reinterpret_cast<QAngle_t*>(reinterpret_cast<std::uintptr_t>(weapon) + firePunchOffset);
        auto** current = reinterpret_cast<CUserCmd**>(reinterpret_cast<std::uintptr_t>(controller) + fireCommandOffset);
        CUserCmd* previous = nullptr;
        bool copied = false, installed = false, filled = false;
        __try {
            saved = *cache;
            copied = true;
            previous = *current;
            *current = cmd;
            installed = true;
            attackIndex = historyIndex;
            fillFire(weapon, &raw, secondary ? 1 : 0);
            // FillGunFireData clamps against the current prediction clock. The queued
            // command fires later; use its actual history time for seed, eye and punch.
            const auto identity = *reinterpret_cast<std::uintptr_t*>(reinterpret_cast<std::uintptr_t>(weapon) + 0x10);
            int mapped = 0;
            mapShotTick(&mapped, time.tick, *reinterpret_cast<int*>(identity + 0x38));
            raw.tick = mapped;
            raw.fraction = time.fraction;
            Vector_t eye{};
            raw.eye = *eyeAtTime(&eye, local, &time, 0, nullptr);
            const auto unpunched = raw.angle - raw.punch;
            QAngle_t punch{};
            auto* services = *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(local) + punchServicesOffset);
            punchAtTime(services, &punch, &raw, 1);
            raw.punch = punch;
            raw.angle = unpunched + raw.punch;
            filled = true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            filled = false;
        }
        attackIndex = index;
        if (installed) {
            __try {
                *current = previous;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                filled = false;
            }
        }
        if (copied) {
            __try {
                *cache = saved;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                filled = false;
            }
        }
        if (!filled || !raw.history || raw.source != 0 || raw.tick <= 0 || !std::isfinite(raw.fraction) ||
            raw.fraction < 0 || raw.fraction >= 1 || !raw.angle.IsValid() || !Bones::IsValidPos(raw.eye))
            return false;
        shot = {raw.tick, raw.fraction, raw.eye, raw.angle, raw.punch};
        return true;
    }

    static bool
    DirectionsForSeed(const QAngle_t& angle, std::uint32_t seed, const AccuracyState& a, Vector_t* directions) {
        if (!a.valid || !directions || !angle.IsValid())
            return false;
        __try {
            const bool patterned = Cvar::Bool("weapon_accuracy_shotgun_spread_patterns", true);
            const bool maximum = Cvar::Bool("weapon_debug_max_inaccuracy", false),
                       upOnly = Cvar::Bool("weapon_debug_inaccuracy_only_up", false);
            const unsigned char* pattern = nullptr;
            if (patterned) {
                if (!spreadPatterns || !findSpreadPattern)
                    return false;
                const auto definition = static_cast<std::uint16_t>(a.definition);
                const int index = findSpreadPattern(spreadPatterns, &definition, spreadPatterns);
                if (index < 0)
                    return false;
                const auto storage =
                    *reinterpret_cast<std::uintptr_t*>(reinterpret_cast<std::uintptr_t>(spreadPatterns) + 0x10);
                pattern = *reinterpret_cast<const unsigned char**>(storage + index * 32 + 0x18);
                if (!pattern)
                    return false;
            }
            ValveRng rng;
            rng.Seed(static_cast<int>(seed + 1u));
            Vector_t forward, right, up;
            angle.ToDirections(&forward, &right, &up);
            float inaccuracy = 0, inaccuracyAngle = 0;
            for (int pellet = 0; pellet < a.bullets; pellet++) {
                if (pellet == 0 || patterned) {
                    const float radius = rng.RandomFloat(0, 1);
                    inaccuracyAngle = rng.RandomFloat(0, kTwoPi);
                    inaccuracy = (maximum ? 1.f : Reshape(radius, a.recoil, a.definition, a.mode)) * a.inaccuracy;
                    if (upOnly)
                        inaccuracyAngle = kTwoPi * .25f;
                }
                float radius, spreadAngle;
                const int index = static_cast<int>(a.recoil) * a.bullets + pellet;
                if (patterned) {
                    if (!pattern[0x604] && index >= 0 && index < 64) {
                        spreadAngle = *reinterpret_cast<const float*>(pattern + 0x404 + index * 8);
                        radius = *reinterpret_cast<const float*>(pattern + 0x408 + index * 8);
                    } else {
                        spreadAngle = rng.RandomFloat(0, kTwoPi);
                        radius = rng.RandomFloat(0, 1);
                    }
                } else {
                    radius = rng.RandomFloat(0, 1);
                    spreadAngle = rng.RandomFloat(0, kTwoPi);
                }
                const float spread = (maximum ? 1.f : Reshape(radius, a.recoil, a.definition, a.mode)) * a.spread;
                if (upOnly)
                    spreadAngle = kTwoPi * .25f;
                const float x = std::cos(inaccuracyAngle) * inaccuracy + std::cos(spreadAngle) * spread;
                const float y = std::sin(inaccuracyAngle) * inaccuracy + std::sin(spreadAngle) * spread;
                auto direction = forward - right * x + up * y;
                const float length = direction.Length();
                if (!std::isfinite(length) || length <= 0)
                    return false;
                directions[pellet] = direction * (1.f / length);
            }
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    bool SeedDirections(const QAngle_t& angle, int tick, const AccuracyState& a, Vector_t* directions) {
        if (!computeSeed || tick <= 0 || !angle.IsValid())
            return false;
        __try {
            return DirectionsForSeed(angle, computeSeed(nullptr, &angle, tick), a, directions);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    bool SampleDirections(const QAngle_t& angle, int sample, const AccuracyState& a, Vector_t* directions) {
        return sample >= 0 && sample < 256 &&
               DirectionsForSeed(angle, static_cast<std::uint32_t>(sample), a, directions);
    }

    struct HCSpreadSamples {
        struct PatternPoint {
            std::uint32_t angle = 0, radius = 0;
            bool fixed = false;
            bool operator==(const PatternPoint&) const = default;
        };

        struct Key {
            std::uint32_t inaccuracy = 0, spread = 0, recoil = 0;
            int definition = 0, mode = 0, bullets = 0;
            bool patterned = false, maximum = false, upOnly = false;
            std::array<PatternPoint, 16> pattern{};
            bool operator==(const Key&) const = default;
        } key{};

        mutable std::array<std::array<std::array<float, 2>, 16>, 256> offsets{};
        mutable std::array<bool, 256> ready{};
        bool initialized = false;
    };

    bool PrepareHCSamples(const QAngle_t& angle, const AccuracyState& a, HCSampleContext& context) {
        context = {};
        if (!a.valid || !angle.IsValid())
            return false;
        HCSpreadSamples::Key key{};
        key.inaccuracy = std::bit_cast<std::uint32_t>(a.inaccuracy);
        key.spread = std::bit_cast<std::uint32_t>(a.spread);
        key.recoil = std::bit_cast<std::uint32_t>(a.recoil);
        key.definition = a.definition;
        key.mode = a.mode;
        key.bullets = a.bullets;
        __try {
            key.patterned = Cvar::Bool("weapon_accuracy_shotgun_spread_patterns", true);
            key.maximum = Cvar::Bool("weapon_debug_max_inaccuracy", false);
            key.upOnly = Cvar::Bool("weapon_debug_inaccuracy_only_up", false);
            if (key.patterned) {
                if (!spreadPatterns || !findSpreadPattern)
                    return false;
                const auto definition = static_cast<std::uint16_t>(a.definition);
                const int found = findSpreadPattern(spreadPatterns, &definition, spreadPatterns);
                if (found < 0)
                    return false;
                const auto storage =
                    *reinterpret_cast<std::uintptr_t*>(reinterpret_cast<std::uintptr_t>(spreadPatterns) + 0x10);
                const auto* pattern = *reinterpret_cast<const unsigned char**>(storage + found * 32 + 0x18);
                if (!pattern)
                    return false;
                for (int pellet = 0; pellet < a.bullets; ++pellet) {
                    const int index = static_cast<int>(a.recoil) * a.bullets + pellet;
                    if (!pattern[0x604] && index >= 0 && index < 64)
                        key.pattern[pellet] = {
                            std::bit_cast<std::uint32_t>(*reinterpret_cast<const float*>(pattern + 0x404 + index * 8)),
                            std::bit_cast<std::uint32_t>(*reinterpret_cast<const float*>(pattern + 0x408 + index * 8)),
                            true};
                }
            }
            // HC runs serially on the game thread. Do not introduce TLS: this DLL also
            // supports manual mapping, whose loader does not initialize TLS storage.
            // Auto HC alternates current and recovered accuracy. Retain both
            // deterministic sets so each candidate does not regenerate all RNG samples.
            static std::array<HCSpreadSamples, 2> cache;
            static int recent = 0;
            int slot = recent;
            if (!cache[slot].initialized || !(cache[slot].key == key))
                slot = 1 - slot;
            auto& samples = cache[slot];
            if (!samples.initialized || !(samples.key == key)) {
                samples.key = key;
                samples.ready.fill(false);
                samples.initialized = true;
            }
            recent = slot;
            angle.ToDirections(&context.forward, &context.right, &context.up);
            context.spread = &samples;
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    bool SampleHCDirections(const HCSampleContext& context, int sample, Vector_t* directions) {
        if (!context.spread || !directions || sample < 0 || sample >= 256)
            return false;
        const auto& samples = *context.spread;
        const auto& key = samples.key;
        if (!samples.ready[sample]) {
            ValveRng rng;
            rng.Seed(sample + 1);
            const float sourceInaccuracy = std::bit_cast<float>(key.inaccuracy),
                        sourceSpread = std::bit_cast<float>(key.spread), recoil = std::bit_cast<float>(key.recoil);
            float inaccuracy = 0, inaccuracyAngle = 0;
            for (int pellet = 0; pellet < key.bullets; ++pellet) {
                if (pellet == 0 || key.patterned) {
                    const float radius = rng.RandomFloat(0, 1);
                    inaccuracyAngle = rng.RandomFloat(0, kTwoPi);
                    inaccuracy =
                        (key.maximum ? 1.f : Reshape(radius, recoil, key.definition, key.mode)) * sourceInaccuracy;
                    if (key.upOnly)
                        inaccuracyAngle = kTwoPi * .25f;
                }
                float radius, spreadAngle;
                // Keep the native path's RNG order, including no spread draws for fixed
                // patterns and the shared inaccuracy term when patterns are disabled.
                if (key.patterned) {
                    const auto& point = key.pattern[pellet];
                    if (point.fixed) {
                        spreadAngle = std::bit_cast<float>(point.angle);
                        radius = std::bit_cast<float>(point.radius);
                    } else {
                        spreadAngle = rng.RandomFloat(0, kTwoPi);
                        radius = rng.RandomFloat(0, 1);
                    }
                } else {
                    radius = rng.RandomFloat(0, 1);
                    spreadAngle = rng.RandomFloat(0, kTwoPi);
                }
                const float spread =
                    (key.maximum ? 1.f : Reshape(radius, recoil, key.definition, key.mode)) * sourceSpread;
                if (key.upOnly)
                    spreadAngle = kTwoPi * .25f;
                samples.offsets[sample][pellet] = {
                    std::cos(inaccuracyAngle) * inaccuracy + std::cos(spreadAngle) * spread,
                    std::sin(inaccuracyAngle) * inaccuracy + std::sin(spreadAngle) * spread};
            }
            samples.ready[sample] = true;
        }
        for (int pellet = 0; pellet < key.bullets; ++pellet) {
            const auto& offset = samples.offsets[sample][pellet];
            auto direction = context.forward - context.right * offset[0] + context.up * offset[1];
            const float length = direction.Length();
            if (!std::isfinite(length) || length <= 0)
                return false;
            directions[pellet] = direction * (1.f / length);
        }
        return true;
    }

    bool RecoilAffectsSpreadPattern(const AccuracyState& a) {
        if (a.definition == 28)
            return true;
        if (!Cvar::Bool("weapon_accuracy_shotgun_spread_patterns", true))
            return false;
        __try {
            if (!spreadPatterns || !findSpreadPattern)
                return true;
            const auto definition = static_cast<std::uint16_t>(a.definition);
            const int index = findSpreadPattern(spreadPatterns, &definition, spreadPatterns);
            if (index < 0)
                return true;
            const auto storage =
                *reinterpret_cast<std::uintptr_t*>(reinterpret_cast<std::uintptr_t>(spreadPatterns) + 0x10);
            const auto* pattern = *reinterpret_cast<const unsigned char**>(storage + index * 32 + 0x18);
            return !pattern || !pattern[0x604];
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return true;
        }
    }

    AccuracyState ReadAccuracy(C_CSWeaponBase* weapon) {
        AccuracyState state{};
        state.weapon = weapon;
        if (!getInaccuracy || !getSpread || !weapon)
            return state;
        __try {
            state.inaccuracy = getInaccuracy(weapon, &state.movement, &state.air);
            state.spread = getSpread(weapon);
            state.recovery = getRecovery ? getRecovery(weapon) : 0.f;
            state.recoil = weapon->m_flRecoilIndex();
            state.definition = weapon->m_iItemDefinitionIndex();
            state.mode = weapon->m_weaponMode();
            if (auto* data = weapon->Data())
                state.bullets = std::clamp(data->m_nNumBullets(), 1, 16);
            state.valid = std::isfinite(state.inaccuracy) && state.inaccuracy >= 0 && std::isfinite(state.spread) &&
                          state.spread >= 0 && std::isfinite(state.movement) && std::isfinite(state.air);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            state = {};
        }
        return state;
    }

    AccuracyState ReadAccuracyMode(C_CSWeaponBase* weapon, int mode) {
        if (!weapon)
            return {};
        const int previous = weapon->m_weaponMode();
        AccuracyState result{};
        __try {
            weapon->m_weaponMode() = std::clamp(mode, 0, 1);
            result = ReadAccuracy(weapon);
        } __finally {
            weapon->m_weaponMode() = previous;
        }
        return result;
    }

    AccuracyState ReadFireAccuracy(
        C_CSPlayerPawn* local, C_CSWeaponBase* weapon, float* lower, float* upper, const Vector_t* fireVelocity) {
        auto state = ReadAccuracy(weapon);
        if (lower)
            *lower = state.inaccuracy;
        if (upper)
            *upper = state.inaccuracy;
        if (!state.valid || !local)
            return state;
        auto* data = weapon->Data();
        if (!data)
            return state;
        const bool grounded = (local->m_fFlags() & FL_ONGROUND) != 0;
        const auto offset = grounded && (local->m_fFlags() & FL_DUCKING)
                                ? SchemaFinder::Get(hash_32_fnv1a_const("CCSWeaponBaseVData->m_flInaccuracyCrouch"))
                                : SchemaFinder::Get(hash_32_fnv1a_const("CCSWeaponBaseVData->m_flInaccuracyStand"));
        if (!offset)
            return state;
        float baseline = *reinterpret_cast<float*>(reinterpret_cast<std::uintptr_t>(data) + offset +
                                                   std::clamp(state.mode, 0, 1) * sizeof(float));
        if (!grounded) {
            const auto jump = SchemaFinder::Get(hash_32_fnv1a_const("CCSWeaponBaseVData->m_flInaccuracyJump"));
            if (!jump)
                return state;
            // Native UpdateAccuracyPenalty recovers toward stand + jump in the air.
            // The vertical-speed term below is a separate contribution.
            baseline += *reinterpret_cast<float*>(reinterpret_cast<std::uintptr_t>(data) + jump +
                                                  std::clamp(state.mode, 0, 1) * sizeof(float)) *
                        Cvar::Float("weapon_air_spread_scale", 1.f);
        }
        const float penalty = weapon->m_fAccuracyPenalty();
        state.minimumInaccuracy = grounded ? baseline : -1.f;
        // Weapon accuracy is updated once between this command and primary attack.
        state.inaccuracy = (std::max)(0.f,
                                      state.inaccuracy - penalty +
                                          LegitMath::RecoverAccuracy(penalty, baseline, state.recovery, 1.f / 64.f));
        if (!(local->m_fFlags() & FL_ONGROUND)) {
            const auto initialOff =
                SchemaFinder::Get(hash_32_fnv1a_const("CCSWeaponBaseVData->m_flInaccuracyJumpInitial"));
            const auto apexOff = SchemaFinder::Get(hash_32_fnv1a_const("CCSWeaponBaseVData->m_flInaccuracyJumpApex"));
            const auto gravityOff = SchemaFinder::Get(hash_32_fnv1a_const("C_BaseEntity->m_flGravityScale"));
            if (initialOff && apexOff && gravityOff) {
                const auto address = reinterpret_cast<std::uintptr_t>(data);
                const float scale = Cvar::Float("weapon_air_spread_scale", 1.f);
                const float initial = *reinterpret_cast<float*>(address + initialOff) * scale;
                const float apex = *reinterpret_cast<float*>(address + apexOff) * scale;
                state.minimumInaccuracy =
                    baseline + LegitMath::AirInaccuracy(0.f, Cvar::Float("sv_jump_impulse", 301.99338f), initial, apex);
                const float gravity = *reinterpret_cast<float*>(reinterpret_cast<std::uintptr_t>(local) + gravityOff) *
                                      Cvar::Float("sv_gravity", 800.f);
                const float nextZ = fireVelocity ? fireVelocity->z : local->m_vecVelocity().z - gravity / 64.f;
                const float air =
                    LegitMath::AirInaccuracy(nextZ, Cvar::Float("sv_jump_impulse", 301.99338f), initial, apex);
                state.inaccuracy = (std::max)(0.f, state.inaccuracy - state.air + air);
                state.air = air;
            }
        }
        if (fireVelocity) {
            const auto maxOff = SchemaFinder::Get(hash_32_fnv1a_const("CCSWeaponBaseVData->m_flMaxSpeed"));
            const auto moveOff = SchemaFinder::Get(hash_32_fnv1a_const("CCSWeaponBaseVData->m_flInaccuracyMove"));
            const auto walkOff = SchemaFinder::Get(hash_32_fnv1a_const("C_CSPlayerPawn->m_bIsWalking"));
            if (maxOff && moveOff) {
                const auto mode = std::clamp(state.mode, 0, 1) * sizeof(float);
                const float maximum = *reinterpret_cast<float*>(reinterpret_cast<std::uintptr_t>(data) + maxOff + mode);
                const float coefficient =
                    *reinterpret_cast<float*>(reinterpret_cast<std::uintptr_t>(data) + moveOff + mode);
                if (maximum > 0.f) {
                    const float ratio =
                        std::clamp((std::hypot(fireVelocity->x, fireVelocity->y) / maximum - .34f) / .61f, 0.f, 1.f);
                    const bool walking =
                        walkOff && *reinterpret_cast<bool*>(reinterpret_cast<std::uintptr_t>(local) + walkOff);
                    const float movement = coefficient * (walking ? ratio : std::pow(ratio, .25f));
                    state.inaccuracy = (std::max)(0.f, state.inaccuracy - state.movement + movement);
                    state.movement = movement;
                }
            }
        }
        if (lower)
            *lower = state.inaccuracy;
        if (upper)
            *upper = state.inaccuracy;
        if (!(local->m_fFlags() & FL_ONGROUND))
            return state;
        if (lower && upper) {
            const auto maxOff = SchemaFinder::Get(hash_32_fnv1a_const("CCSWeaponBaseVData->m_flMaxSpeed"));
            const auto moveOff = SchemaFinder::Get(hash_32_fnv1a_const("CCSWeaponBaseVData->m_flInaccuracyMove"));
            const auto walkOff = SchemaFinder::Get(hash_32_fnv1a_const("C_CSPlayerPawn->m_bIsWalking"));
            if (maxOff && moveOff) {
                const auto mode = std::clamp(state.mode, 0, 1) * sizeof(float);
                const float maximum = *reinterpret_cast<float*>(reinterpret_cast<std::uintptr_t>(data) + maxOff + mode);
                const float coefficient =
                    *reinterpret_cast<float*>(reinterpret_cast<std::uintptr_t>(data) + moveOff + mode);
                const auto velocity = local->m_vecVelocity();
                const float speed = std::hypot(velocity.x, velocity.y);
                const auto services = reinterpret_cast<std::uintptr_t>(local->m_pMovementServices());
                const float surface = services ? *reinterpret_cast<float*>(services + 0x26c) : 1.f;
                const float change =
                    (Cvar::Float("sv_accelerate", 5.5f) * maximum +
                     Cvar::Float("sv_friction", 5.2f) * (std::max)(speed, Cvar::Float("sv_stopspeed", 80.f))) *
                    surface / 64.f;
                const bool walking =
                    walkOff && *reinterpret_cast<bool*>(reinterpret_cast<std::uintptr_t>(local) + walkOff);
                const auto movement = [&](float v) {
                    const float ratio = std::clamp((v / maximum - .34f) / .61f, 0.f, 1.f);
                    return coefficient * (walking ? ratio : std::pow(ratio, .25f));
                };
                // Input may change velocity before primary attack. Do not authorize a
                // lucky ray that misses as movement spread crosses the walking threshold.
                if (maximum > 0.f) {
                    const float controlled = PlannedSpeedLimit();
                    const float fastest = controlled > 0.f ? (std::max)(speed, controlled) : speed + change;
                    *lower =
                        (std::max)(0.f, state.inaccuracy - state.movement + movement((std::max)(0.f, speed - change)));
                    // Autostop has already replaced the movement command with braking pulses.
                    // Treating it as full acceleration invents spread that the command cannot
                    // reach, rejecting valid seeds throughout a sustained controlled peek.
                    *upper = (std::max)(*lower, state.inaccuracy - state.movement + movement(fastest));
                }
            }
        }
        return state;
    }

    AccuracyState
    ReadFireAccuracyMode(C_CSPlayerPawn* local, C_CSWeaponBase* weapon, int mode, const Vector_t* fireVelocity) {
        if (!weapon)
            return {};
        const int previous = weapon->m_weaponMode();
        AccuracyState result{};
        __try {
            weapon->m_weaponMode() = std::clamp(mode, 0, 1);
            result = ReadFireAccuracy(local, weapon, nullptr, nullptr, fireVelocity);
        } __finally {
            weapon->m_weaponMode() = previous;
        }
        return result;
    }

    bool PassesHC(const Vector_t& eye,
                  const QAngle_t& angle,
                  float range,
                  const AccuracyState& a,
                  const Bones::GroupCapsule* boxes,
                  int count,
                  float percent,
                  C_CSPlayerPawn* local,
                  C_CSPlayerPawn* target,
                  int minimumDamage,
                  int budget) {
        if (percent <= 0.f)
            return true;
        if (!a.valid || !boxes || count <= 0 || range <= 0)
            return false;
        const int samples = 64 << std::clamp(budget, 0, 2);
        const int needed = static_cast<int>(std::ceil(std::clamp(percent, 0.f, 100.f) * samples * .01f));
        Vector_t forward{}, right{}, up{};
        angle.ToDirections(&forward, &right, &up);
        int hits = 0;
        const float cone = std::atan(a.inaccuracy + a.spread);
        bool reachable = false;
        for (int i = 0; i < count; i++) {
            const auto delta = boxes[i].capsule.center - eye;
            const float distance = delta.Length();
            const float radius =
                (boxes[i].capsule.maxs - boxes[i].capsule.mins).Length() * .5f + boxes[i].capsule.radius;
            if (distance <= radius) {
                reachable = true;
                break;
            }
            const float dot = (forward.x * delta.x + forward.y * delta.y + forward.z * delta.z) / distance;
            if (dot >= std::cos((std::min)(3.14159265f, cone + std::asin(std::clamp(radius / distance, 0.f, 1.f))))) {
                reachable = true;
                break;
            }
        }
        if (!reachable)
            return false;
        for (int seed = 0; seed < samples; ++seed) {
            float x[16]{}, y[16]{};
            CalcSpreadValve(seed + 1,
                            a.inaccuracy,
                            a.spread,
                            a.recoil,
                            static_cast<std::uint16_t>(a.definition),
                            a.mode,
                            a.bullets,
                            x,
                            y);
            bool hit = false;
            float totalDamage = 0.f;
            for (int pellet = 0; pellet < a.bullets && !hit; ++pellet) {
                auto direction = forward - right * x[pellet] + up * y[pellet];
                const float length = direction.Length();
                if (length <= 0.f)
                    continue;
                direction = direction * (1.f / length);
                float distance = range;
                int group = -1;
                for (int i = 0; i < count; ++i) {
                    const float d = RayCapsule(
                        eye, direction, range, boxes[i].capsule.mins, boxes[i].capsule.maxs, boxes[i].capsule.radius);
                    if (d < distance) {
                        distance = d;
                        group = boxes[i].group;
                    }
                }
                if (group >= 0) {
                    if (!local || !target)
                        hit = true;
                    else {
                        totalDamage +=
                            (std::max)(0.f,
                                       ShotDamage(
                                           eye, eye + direction * (distance + .01f), group, local, target, a.weapon));
                        hit = totalDamage >= (std::max)(1, minimumDamage);
                    }
                }
            }
            hits += hit;
            if (hits >= needed)
                return true;
            if (hits + samples - seed - 1 < needed)
                return false;
        }
        return false;
    }
} // namespace Legitbot
