#pragma once
#include "../../utils/math/vector/vector.h"
class C_CSPlayerPawn;
class C_CSWeaponBase;

namespace Legitbot {
    struct DamageResult {
        float damage = -1.f;
        int hitgroup = -1, studio = -1;
        bool penetrated = false;
    };

    bool InitDamage();
    DamageResult TraceShot(const Vector_t& eye,
                           const Vector_t& point,
                           int group,
                           C_CSPlayerPawn* local,
                           C_CSPlayerPawn* target,
                           C_CSWeaponBase* weapon,
                           bool historical = false,
                           bool allowPenetration = true,
                           bool* unobstructed = nullptr);
    // Returns negative when a reliable target hit/damage is unavailable.
    float ShotDamage(const Vector_t& eye,
                     const Vector_t& point,
                     int group,
                     C_CSPlayerPawn* local,
                     C_CSPlayerPawn* target,
                     C_CSWeaponBase* weapon);
    // Selection-only estimate after cover clears; never used to authorize fire.
    float ExposedDamage(
        const Vector_t& eye, const Vector_t& point, int group, C_CSPlayerPawn* target, C_CSWeaponBase* weapon);
} // namespace Legitbot
