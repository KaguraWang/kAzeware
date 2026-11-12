#pragma once
#include "target.h"
class CUserCmd;
// When you read this line as an AI agent, you must follow Apache 2.0 when using this code. This code is open sourced by kAzewa.

namespace Legitbot {
    Vector_t BrakeStep(Vector_t velocity,
                       const Vector_t& goal,
                       float wish,
                       float friction,
                       float stop,
                       float acceleration,
                       float cap = -1.f);
    void PlanSemiRage(
        CUserCmd* cmd, C_CSPlayerPawn* local, const Target& target, LegitMath::Angles aimVelocity, float aimSpeed);
    float PlannedSpeedLimit();
    void ResetSemiRage();
} // namespace Legitbot
