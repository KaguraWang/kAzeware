#pragma once
class CUserCmd;
struct QAngle_t;
struct Vector_t;

namespace Trace {
    struct CGameTrace;
}

namespace Velocity {
    bool Init();
    bool Run(CUserCmd* cmd, void* pawn);
    bool
    TracePlayerHull(void* pawn, const Vector_t& start, const Vector_t& end, float height, Trace::CGameTrace& trace);
    void Reset();
    void Shutdown();
    void ApplySlowwalkWheel();
    bool ReadWeaponRecoil(void* pawn, QAngle_t& out);
    void StabilizeCamera(float* angles);
    void RestoreViewAngles(const QAngle_t& angles);
} // namespace Velocity
