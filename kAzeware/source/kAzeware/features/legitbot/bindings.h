#pragma once
#include "../../../../external/json/json.hpp"
class Keybinds;

namespace LegitBinds {
    void Register(Keybinds& keys);
    void Migrate(const nlohmann::json& legacy);
} // namespace LegitBinds
