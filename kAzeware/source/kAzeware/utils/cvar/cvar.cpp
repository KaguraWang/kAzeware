#include "cvar.h"

#include <Windows.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Cvar {
    namespace {

        constexpr std::uintptr_t kCvarDataOffset = 0x08;
        constexpr std::uintptr_t kCvarValueOffset = 0x58;

        struct NameHash {
            using is_transparent = void;

            std::size_t operator()(std::string_view name) const noexcept {
                return std::hash<std::string_view>{}(name);
            }
        };

        // Own keys across caller/module lifetimes; heterogeneous lookup keeps the
        // hot path allocation-free even when the caller only supplies a const char*.
        std::unordered_map<std::string, std::uintptr_t, NameHash, std::equal_to<>> g_cache;
        SRWLOCK g_cacheLock = SRWLOCK_INIT;

        std::uintptr_t ResolveRipRel3(std::uintptr_t insn) {
            const std::int32_t disp = *reinterpret_cast<const std::int32_t*>(insn + 3);
            return insn + 7 + static_cast<std::uintptr_t>(static_cast<std::intptr_t>(disp));
        }

        std::uintptr_t FindCStringInRange(const char* sz, std::uintptr_t start, std::uintptr_t end) {
            const size_t len = std::strlen(sz) + 1;
            if (end <= start || end - start < len)
                return 0;
            for (std::uintptr_t p = start; p + len <= end; ++p) {
                if (std::memcmp(reinterpret_cast<const void*>(p), sz, len) == 0)
                    return p;
            }
            return 0;
        }

        std::uintptr_t ResolveConVarInModule(HMODULE hMod, const char* name) {
            if (!hMod)
                return 0;
            const auto* pDos = reinterpret_cast<PIMAGE_DOS_HEADER>(hMod);
            if (pDos->e_magic != IMAGE_DOS_SIGNATURE)
                return 0;
            const auto* pNt =
                reinterpret_cast<PIMAGE_NT_HEADERS64>(reinterpret_cast<std::uintptr_t>(pDos) + pDos->e_lfanew);
            if (pNt->Signature != IMAGE_NT_SIGNATURE)
                return 0;

            const std::uintptr_t imageBase = reinterpret_cast<std::uintptr_t>(hMod);
            const std::uintptr_t imageEnd = imageBase + pNt->OptionalHeader.SizeOfImage;
            const std::uintptr_t codeStart = imageBase + pNt->OptionalHeader.BaseOfCode;
            const std::uintptr_t codeEnd = codeStart + pNt->OptionalHeader.SizeOfCode;

            const std::uintptr_t strAddr = FindCStringInRange(name, imageBase, imageEnd);
            if (!strAddr)
                return 0;

            // Constructors may set up flags/defaults between the name and object LEAs.
            // sv_airaccelerate in build 14192 has this longer form.
            for (std::uintptr_t p = codeStart; p + 14 <= codeEnd; ++p) {
                const auto* b = reinterpret_cast<const std::uint8_t*>(p);
                if (b[0] != 0x48 || b[1] != 0x8D || b[2] != 0x15)
                    continue;
                if (ResolveRipRel3(p) != strAddr)
                    continue;
                for (std::uintptr_t leaRcx = p + 7; leaRcx + 7 <= codeEnd && leaRcx <= p + 128; ++leaRcx) {
                    const auto* c = reinterpret_cast<const std::uint8_t*>(leaRcx);
                    if (c[0] != 0x48 || c[1] != 0x8D || c[2] != 0x0D)
                        continue;
                    const auto object = ResolveRipRel3(leaRcx);
                    if (object < imageBase || object + 16 > imageEnd)
                        continue;
                    __try {
                        const auto data = *reinterpret_cast<const std::uintptr_t*>(object + kCvarDataOffset);
                        if (!data)
                            continue;
                        const auto resolvedName = *reinterpret_cast<const char* const*>(data);
                        if (resolvedName && std::strcmp(resolvedName, name) == 0)
                            return object;
                    } __except (EXCEPTION_EXECUTE_HANDLER) {
                    }
                }
            }
            return 0;
        }

        std::uintptr_t ResolveConVar(const char* name) {
            AcquireSRWLockShared(&g_cacheLock);
            const auto found = g_cache.find(name);
            if (found != g_cache.end()) {
                const auto value = found->second;
                ReleaseSRWLockShared(&g_cacheLock);
                return value;
            }
            ReleaseSRWLockShared(&g_cacheLock);

            std::uintptr_t addr = 0;
            HMODULE h = GetModuleHandleA("client.dll");
            if (!h)
                h = GetModuleHandleA("client");
            if (!h)
                h = GetModuleHandleA("engine2.dll");
            if (!h)
                h = GetModuleHandleA("server.dll");
            if (h) {
                addr = ResolveConVarInModule(h, name);
                if (!addr) {
                    // Fallback sweep across the same module set.
                    const char* mods[] = {"client.dll", "engine2.dll", "server.dll", "particles.dll"};
                    for (const char* m : mods) {
                        if (HMODULE hm = GetModuleHandleA(m)) {
                            addr = ResolveConVarInModule(hm, name);
                            if (addr)
                                break;
                        }
                    }
                }
            }

            AcquireSRWLockExclusive(&g_cacheLock);
            const auto value = g_cache.emplace(name, addr).first->second;
            ReleaseSRWLockExclusive(&g_cacheLock);
            return value;
        }

    } // namespace

    float Float(const char* name, float fallback) {
        if (!name || !name[0])
            return fallback;

        const std::uintptr_t convar = ResolveConVar(name);
        if (!convar)
            return fallback;

        __try {
            const std::uintptr_t data = *reinterpret_cast<std::uintptr_t*>(convar + kCvarDataOffset);
            if (!data || data < 0x10000ull)
                return fallback;
            const float v = *reinterpret_cast<const float*>(data + kCvarValueOffset);
            return (v == v) ? v : fallback; // NaN guard
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return fallback;
        }
    }

    bool Bool(const char* name, bool fallback) {
        const auto convar = ResolveConVar(name);
        if (!convar)
            return fallback;
        __try {
            const auto data = *reinterpret_cast<const std::uintptr_t*>(convar + kCvarDataOffset);
            return data ? *reinterpret_cast<const bool*>(data + kCvarValueOffset) : fallback;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return fallback;
        }
    }

    int* IntegerValue(const char* name) {
        const auto convar = ResolveConVar(name);
        if (!convar)
            return nullptr;
        __try {
            const auto data = *reinterpret_cast<const std::uintptr_t*>(convar + kCvarDataOffset);
            return data ? reinterpret_cast<int*>(data + kCvarValueOffset) : nullptr;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return nullptr;
        }
    }

    void Warmup() {
        IntegerValue("cl_drawhud");
        IntegerValue("cl_drawhud_force_radar");
        IntegerValue("r_csgo_water_effects");
        IntegerValue("r_drawparticles");
        Bool("sv_usercmd_custom_random_seed", false);
        Bool("sv_unlag", true);
        Float("sv_maxunlag", .2f);
        // Convars read on movement and visual hot paths.
        // First resolution scans the whole client.dll image - eager here means
        // the first in-game use never hitches.
        Float("sensitivity", 2.5f);
        Float("molotov_throw_detonate_time", 2.f);
        Float("weapon_molotov_maxdetonateslope", 90.f);
        Float("inferno_max_range", 150.f);
        Float("inferno_flame_spacing", 42.f);
        Float("sv_hegrenade_radius_multiplier", 1.f);
        Float("sv_hegrenade_damage_multiplier", 1.f);
        Float("sv_gravity", 800.f);
        Float("sv_accelerate", 5.5f);
        Float("sv_friction", 5.2f);
        Float("sv_stopspeed", 80.f);
        Float("sv_stepsize", 18.f);
        Float("sv_jump_spam_penalty_time", 1.f / 64.f);
        Float("weapon_air_spread_scale", 1.f);
        Float("sv_jump_impulse", 301.99338f);
        Float("sv_standable_normal", 0.7f);
        Float("sv_autobunnyhopping", 0.f);
        Float("sv_legacy_jump", 0.f);
        Float("sv_airaccelerate", 12.f);
        Float("sv_maxspeed", 250.f);
        Float("sv_air_max_wishspeed", 30.f);
        Float("sv_quantize_movement_input", 1.f);
    }

} // namespace Cvar
