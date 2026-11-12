#pragma once
class CUserCmd;
class C_CSPlayerPawn;

namespace Legitbot {
    struct Target;
    Target CurrentTarget();
    void Run(CUserCmd* cmd, C_CSPlayerPawn* local);
    void UpdateCamera(C_CSPlayerPawn* local);
    void Reset();
    bool IsWeaponSwitching();
    void OnGameEvent(void* event);
} // namespace Legitbot
