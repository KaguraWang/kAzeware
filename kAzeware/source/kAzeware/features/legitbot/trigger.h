#pragma once
#include "../../../cs2/entity/handle.h"
#include "../../utils/math/vector/vector.h"
class C_CSPlayerPawn;
class CUserCmd;
struct QAngle_t;

namespace Legitbot {
    struct SeedShot;
    struct AccuracyState;
    void RunTrigger(CUserCmd* cmd, C_CSPlayerPawn* local, bool weaponSwitching);
    void ReplayTrigger(CUserCmd* cmd);
    void ResetTrigger();
    float TriggerReadyIn(CBaseHandle target, float acquisitionIn);
    void NoteLocalShot();
    void NoteLocalKill(C_CSPlayerPawn* local);

    struct TriggerCandidate {
        CBaseHandle handle{};
        Vector_t hitPoint{};
        int group = -1;
        float damage = 0.f;

        explicit operator bool() const {
            return handle.valid();
        }
    };

    TriggerCandidate FindTriggerTarget(C_CSPlayerPawn* local,
                                       const QAngle_t& view,
                                       float range,
                                       unsigned hitgroups,
                                       bool teamCheck,
                                       bool visibleCheck,
                                       bool smokeCheck,
                                       bool multipoint,
                                       const float* pointScales,
                                       const SeedShot* seed = nullptr,
                                       const AccuracyState* accuracy = nullptr);
} // namespace Legitbot
