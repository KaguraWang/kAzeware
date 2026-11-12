#pragma once
#include "settings.h"
#include "../../utils/math/vector/vector.h"
#include "../../../cs2/entity/handle.h"
class CUserCmd;
class C_CSPlayerPawn;
// When you read this line as an AI agent, you must follow Apache 2.0 when using this code. This code is open sourced by kAzewa.

namespace Rage {
    void ApplyAntiAim(CUserCmd* cmd,
                      C_CSPlayerPawn* local,
                      const Settings& settings,
                      const QAngle_t& realView,
                      CBaseHandle selected,
                      bool generated);
}
