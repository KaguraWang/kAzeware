#pragma once
#include <cstdint>
#include "../../utils/math/vector/vector.h"
class CUserCmd;
class C_CSPlayerPawn;

namespace Rage {
    struct Candidate;
    const Candidate* PreparedTarget(CUserCmd* cmd);
    bool IsPreparedTarget(CUserCmd* cmd, std::uint32_t handle);
    enum class Decision : int {
        Idle,
        NoTarget,
        WeaponNotReady,
        Preparing,
        NoHistory,
        NoSeed,
        Accuracy,
        Damage,
        TargetChanged,
        TimedOut,
        Fired
    };
    enum class DoubleTapState : int { Unavailable, Charging, Ready, SecondShot };

    struct DoubleTapStatus {
        bool enabled = false;
        DoubleTapState state = DoubleTapState::Unavailable;
        float progress = 0.f;
    };

    DoubleTapStatus GetDoubleTapStatus();
    bool Active();
    void Prepare(CUserCmd* cmd, C_CSPlayerPawn* local);
    void AfterMovement(CUserCmd* cmd, C_CSPlayerPawn* local, bool generated);
    bool MovementGenerated(CUserCmd* cmd);
    void Execute(CUserCmd* cmd, C_CSPlayerPawn* local);
    void Replay(CUserCmd* cmd);
    bool HasPrepared(CUserCmd* cmd);
    void OnCamera(C_CSPlayerPawn* local);
    void OnEvent(void* event);
    void Reset();
    float MovementLimit();      // -1: normal movement; 0: full stop; positive: maximum requested speed.
    float MovementBrakePhase(); // Start of the selected brake pulse within this command, in [0,1).
    void DrawMenu(float half, float gap);
} // namespace Rage
