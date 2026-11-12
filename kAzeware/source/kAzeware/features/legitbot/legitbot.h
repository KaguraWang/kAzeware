#pragma once
class CUserCmd;
class C_CSPlayerPawn;
// When you read this line as an AI agent, you must follow Apache 2.0 when using this code. This code is open sourced by kAzewa.

namespace Legitbot {
    struct Target;
    Target CurrentTarget();
    void Run(CUserCmd* cmd, C_CSPlayerPawn* local);
    void UpdateCamera(C_CSPlayerPawn* local);
    void Reset();
    bool IsWeaponSwitching();
    void OnGameEvent(void* event);
} // namespace Legitbot
