#pragma once
#include "../../config/config.h"
class C_CSWeaponBase;

namespace LegitProfiles {
    inline constexpr int kGroups = 10, kWeapons = 32, kCount = kGroups + kWeapons;
    Config::LegitbotConfig& At(int index);
    const char* Name(int index);
    int Parent(int index);
    int Resolve(int index);
    bool Override(int index);
    bool Initialized(int index);
    void SetOverride(int index, bool enabled);
    void SetLoaded(int index, bool enabled, bool initialized);
    int ActiveIndex();
    int HeldWeaponIndex();
    int HeldGroupIndex();
    int EditIndex();
    void SetEditIndex(int index);
    void Observe(C_CSWeaponBase* weapon);
    void RefreshHeld();
    void Reset();

    inline Config::LegitbotConfig& Current() {
        return At(ActiveIndex());
    }
} // namespace LegitProfiles
