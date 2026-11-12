#pragma once
#include "settings.h"
#include <mutex>
#include "../../../../external/json/json.hpp"
class Keybinds;

namespace Rage {
    std::recursive_mutex& ProfileMutex();
    Settings& Profile(int index);
    Settings Current();
    int CurrentProfile();
    bool ProfileOverride(int index);
    bool ProfileInitialized(int index);
    void SetProfileOverride(int index, bool value);
    void RemoveProfile(int index);
    void RegisterBindings(Keybinds& keys);
    nlohmann::json SaveSettings();
    void LoadSettings(const nlohmann::json& json);
} // namespace Rage
