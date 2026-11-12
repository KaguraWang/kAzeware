#pragma once
#include "../../utils/math/vector/vector.h"
#include "../bones/bones.h"
class C_CSWeaponBase;
class C_CSPlayerPawn;
class CUserCmd;

namespace Legitbot {
    struct AccuracyState {
        float inaccuracy = 0, spread = 0, movement = 0, air = 0, recoil = 0, recovery = 0;
        int definition = 0, mode = 0, bullets = 1;
        C_CSWeaponBase* weapon = nullptr;
        bool valid = false;
        float minimumInaccuracy = -1.f;
    };

    struct SeedShot {
        int tick = 0;
        float fraction = 0;
        Vector_t eye{};
        QAngle_t angle{}, punch{};
    };

    bool ReadSeedShot(CUserCmd* cmd, C_CSPlayerPawn* local, C_CSWeaponBase* weapon, SeedShot& shot);
    // Current input-history clock mapped into this weapon's native time domain.
    // Does not run prediction, install a command or calculate aim/eye state.
    bool ReadShotClock(CUserCmd* cmd, C_CSWeaponBase* weapon, double& clock);
    bool ReadSeedShotAt(CUserCmd* cmd,
                        C_CSPlayerPawn* local,
                        C_CSWeaponBase* weapon,
                        int historyIndex,
                        bool secondary,
                        SeedShot& shot,
                        bool charged = false);
    bool ChargedShotTime(C_CSPlayerPawn* local, C_CSWeaponBase* weapon, int& historyTick, bool secondary = false);
    bool SeedDirections(const QAngle_t& angle, int tick, const AccuracyState& accuracy, Vector_t* directions);
    bool SampleDirections(const QAngle_t& angle, int sample, const AccuracyState& accuracy, Vector_t* directions);
    struct HCSpreadSamples;

    struct HCSampleContext {
        Vector_t forward{}, right{}, up{};
        const HCSpreadSamples* spread = nullptr;
    };

    // Game-thread HC only. Consume this context before the next PrepareHCSamples
    // call; exact spread inputs reuse local offsets while each aim gets one basis.
    bool PrepareHCSamples(const QAngle_t& angle, const AccuracyState& accuracy, HCSampleContext& context);
    bool SampleHCDirections(const HCSampleContext& context, int sample, Vector_t* directions);
    // Read-only cache compatibility: unknown pattern metadata stays sensitive.
    bool RecoilAffectsSpreadPattern(const AccuracyState& accuracy);
    bool InitAccuracy();
    AccuracyState ReadAccuracy(C_CSWeaponBase* weapon);
    AccuracyState ReadAccuracyMode(C_CSWeaponBase* weapon, int mode);
    AccuracyState ReadFireAccuracy(C_CSPlayerPawn* local,
                                   C_CSWeaponBase* weapon,
                                   float* lower = nullptr,
                                   float* upper = nullptr,
                                   const Vector_t* fireVelocity = nullptr);
    AccuracyState ReadFireAccuracyMode(C_CSPlayerPawn* local,
                                       C_CSWeaponBase* weapon,
                                       int mode,
                                       const Vector_t* fireVelocity = nullptr);
    bool PassesHC(const Vector_t& eye,
                  const QAngle_t& angle,
                  float range,
                  const AccuracyState& accuracy,
                  const Bones::GroupCapsule* boxes,
                  int count,
                  float percent,
                  C_CSPlayerPawn* local = nullptr,
                  C_CSPlayerPawn* target = nullptr,
                  int minimumDamage = 0,
                  int budget = 2);
} // namespace Legitbot
