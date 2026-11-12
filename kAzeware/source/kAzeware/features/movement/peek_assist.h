#pragma once
class CUserCmd;
class C_CSPlayerPawn;
struct Vector_t;

namespace PeekAssist {
    float Prepare(CUserCmd* cmd, C_CSPlayerPawn* local);
    void OnEvent(void* event);
    void OnShotCommand(CUserCmd* cmd);
    void Reset();
    void UpdateMarker();
    void StabilizeCamera(Vector_t& origin);
} // namespace PeekAssist
