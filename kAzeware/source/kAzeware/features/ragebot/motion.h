#pragma once
#include "target.h"
#include "shots.h"
class CUserCmd;

namespace Rage {
    void PrepareMotion(CUserCmd* cmd,
                       C_CSPlayerPawn* local,
                       C_CSWeaponBase* weapon,
                       Candidate& target,
                       const Settings& settings,
                       bool intent);
    bool MovementReady(
        CUserCmd* cmd, C_CSPlayerPawn* local, C_CSWeaponBase* weapon, const Settings& settings, bool seedShot);
    bool
    CommandDisplacement(CUserCmd* cmd, C_CSPlayerPawn* local, Vector_t& displacement, Vector_t* velocity = nullptr);
    void ResetMotion();
    bool CanAdvanceOnGround(C_CSPlayerPawn* local, const Vector_t& start, const Vector_t& end);
    // Reuse the movement planner's collision/physics model, with one cached path
    // per enemy and command. This never writes enemy entities or history records.
    void BeginEnemyForecasts();
    bool ForecastEnemyOffset(const TargetPose& pose, int ticks, Vector_t& offset);

    struct PeekTargets {
        Candidate first{}, second{};
    };

    PeekTargets ForecastPeek(CUserCmd* cmd,
                             C_CSPlayerPawn* local,
                             C_CSWeaponBase* weapon,
                             const Settings& settings,
                             const QAngle_t& view,
                             CBaseHandle retained,
                             const Submission& submitted);
} // namespace Rage
