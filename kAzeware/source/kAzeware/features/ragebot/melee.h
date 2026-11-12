#pragma once
#include "shots.h"

namespace Rage {
    bool IsMeleeWeapon(C_CSWeaponBase* weapon);
    void ResetMelee();
    void ReplayMelee(CUserCmd* cmd);
    void RunStandaloneMelee(CUserCmd* cmd, C_CSPlayerPawn* local);
    Decision SubmitMelee(CUserCmd* cmd,
                         C_CSPlayerPawn* local,
                         const Settings& settings,
                         const QAngle_t& view,
                         bool manual,
                         Submission& result);
} // namespace Rage
