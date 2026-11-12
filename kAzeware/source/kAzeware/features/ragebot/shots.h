#pragma once
#include "rage.h"
#include "target.h"
#include "../legitbot/accuracy.h"
class C_CSWeaponBase;

namespace Legitbot {
    struct AccuracyState;
    struct SeedShot;
} // namespace Legitbot

namespace Rage {
    struct Submission {
        bool fired = false, automatic = false, secondary = false, hidden = false;
        int command = 0, index = -1;
        QAngle_t angle{};
        Candidate target{};
        QAngle_t lagCompReference{};
        bool doubleTapReady = false; // Preserve this shot's charge state through command replay.
    };

    Decision SubmitGun(CUserCmd* cmd,
                       C_CSPlayerPawn* local,
                       const Settings& settings,
                       const QAngle_t& view,
                       bool manual,
                       CBaseHandle retained,
                       Submission& result);

    struct ProjectionContext {
        Legitbot::SeedShot first{}, second{};
        bool nativeFirst = false, nativeSecond = false, charged = false;
        double shotTime = 0.;
    };

    bool PrepareProjection(CUserCmd* cmd,
                           C_CSPlayerPawn* local,
                           C_CSWeaponBase* weapon,
                           const Settings& settings,
                           ProjectionContext& context);

    struct ProjectedShot {
        Candidate target{};
        bool lethal = false;
    };

    ProjectedShot SelectProjectedGun(CUserCmd* cmd,
                                     C_CSPlayerPawn* local,
                                     C_CSWeaponBase* weapon,
                                     const Settings& settings,
                                     const ProjectionContext& context,
                                     const QAngle_t& view,
                                     CBaseHandle retained,
                                     const Vector_t& eye,
                                     const Legitbot::AccuracyState& accuracy,
                                     int ticks,
                                     CBaseHandle excluded = {},
                                     ScanBudget* budget = nullptr,
                                     bool secondShot = false,
                                     ScanCursor* cursor = nullptr);
    void ResetShots();
    bool DoubleTapPending(CUserCmd* cmd, bool sameTargetOnly = false);
    bool DoubleTapReservedLethal(CBaseHandle target);
    void ReplayDoubleTap(CUserCmd* cmd);
    void RewriteDoubleTapCommand(CUserCmd* cmd, int historyTick, bool secondary, const QAngle_t* angle = nullptr);
    void SetDoubleTapStatus(DoubleTapStatus status);
    const char* SpreadStatus();
    bool NeedsHitchanceFallback(const Settings& settings);
    Settings EffectiveShotSettings(Settings settings);
    float AutoPointCone(const Legitbot::AccuracyState& accuracy, int mode, int budget);
    bool ProjectedHitchance(C_CSPlayerPawn* local,
                            C_CSWeaponBase* weapon,
                            const Candidate& target,
                            const Settings& settings,
                            const Vector_t& eye,
                            const QAngle_t& angle,
                            const Legitbot::AccuracyState& accuracy,
                            ScanBudget* budget = nullptr);
} // namespace Rage
