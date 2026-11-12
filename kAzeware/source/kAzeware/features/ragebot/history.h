#pragma once
#include "target.h"
class CUserCmd;
class CCSGOInputHistoryEntryPB;

namespace Rage {
    void BindPoseInterpolation(Candidate& pose, const CCSGOInputHistoryEntryPB& entry);
    void CaptureHistory(CUserCmd* cmd, C_CSPlayerPawn* local, const Settings& settings);
    void ObserveShotClock(int clientTick, double shotTime);
    double ForecastShotClock(int clientTick);
    Candidate SelectHistory(CUserCmd* cmd,
                            C_CSPlayerPawn* local,
                            C_CSWeaponBase* weapon,
                            const Vector_t& eye,
                            const QAngle_t& view,
                            const Settings& settings,
                            CBaseHandle retained,
                            double shotTime);
    Candidate SelectShootableHistory(CUserCmd* cmd,
                                     int index,
                                     C_CSPlayerPawn* local,
                                     C_CSWeaponBase* weapon,
                                     const Vector_t& eye,
                                     const QAngle_t& view,
                                     const Settings& settings,
                                     CBaseHandle retained,
                                     double shotTime,
                                     const Candidate& live,
                                     ScanAcceptance acceptance);
    bool ApplyHistory(CUserCmd* cmd, int index, const Candidate& target, const Settings& settings, double shotTime);
    bool CanApplyHistory(CUserCmd* cmd, int index, const Candidate& target, const Settings& settings, double shotTime);
    void ResetHistory();
} // namespace Rage
