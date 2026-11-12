#pragma once
#include "settings.h"
#include "../bones/bones.h"
#include "../../../cs2/entity/handle.h"
#include <span>
#include <chrono>
class C_CSPlayerPawn;
class C_CSWeaponBase;

namespace Rage {
    struct Candidate {
        CBaseHandle handle{};
        Vector_t point{};
        int group = -1, studio = -1, pointIndex = -1, health = 0;
        float damage = 0.f, angle = 0.f, distance = 0.f;
        bool shootable = false;
        bool historical = false;
        bool simulationPose = false;
        bool extrapolated = false;
        float sourceSimulationTime = 0.f;
        int renderTick = 0;
        float renderFraction = 0.f;
        std::array<Bones::GroupCapsule, 32> boxes{};
        int boxCount = 0;
        float seedInset = 0.f;
        Vector_t forecastOffset{}; // Drawing stays attached to the source hitbox.

        explicit operator bool() const {
            return handle.valid() && group >= 0;
        }
    };

    struct RayIntersection {
        float distance = 0.f;
        int group = -1, studio = -1;
    };

    struct TargetPose {
        Candidate candidate{};
        Vector_t origin{}, velocity{};
        float simulationTime = 0.f;
        bool grounded = false;
    };

    int CollectTargetPoses(C_CSPlayerPawn* local,
                           C_CSWeaponBase* weapon,
                           const Vector_t& eye,
                           const QAngle_t& view,
                           const Settings& settings,
                           std::span<TargetPose> output,
                           float extraRange = 0.f);

    // A successful MD ray can still fail HC or seed validation. Continue scanning
    // other points instead of letting that one point block the whole target.
    struct ScanBudget {
        std::chrono::steady_clock::time_point deadline;
        bool exhausted = false;

        bool Expired() {
            return exhausted || (exhausted = std::chrono::steady_clock::now() >= deadline);
        }
    };

    // Scheduling position only: no candidate, pose, damage or collision result
    // survives between slices. A cursor requests the first freshly accepted point.
    struct ScanCursor {
        int target = 0, box = 0, point = 0;
        CBaseHandle pawn{};
    };

    struct ScanAcceptance {
        bool (*test)(void*, const Candidate&) = nullptr;
        void* context = nullptr;
        float damageFloor = 0.f;
        ScanBudget* budget = nullptr;
        float angleCeiling = 180.f;
        int firstPoint = 0;
        int lastPoint = 5;
    };

    void BeginTargetScan(int command, C_CSPlayerPawn* local);
    void RefreshTargetPoses();
    void ResetTargetScan();
    QAngle_t AimAngle(const Vector_t& eye, const Vector_t& point);
    bool InAimCone(const Vector_t& delta, const QAngle_t& view, int fov);
    bool Prefer(const Candidate& candidate, const Candidate& current, const Settings& settings, CBaseHandle retained);
    int RayStudio(const Candidate& candidate, const Vector_t& eye, const Vector_t& ray, float range);
    float SeedPoseInset(const Vector_t& velocity);
    RayIntersection SeedIntersection(const Candidate& candidate, const Vector_t& eye, const Vector_t& ray, float range);
    bool CenterVisible(C_CSPlayerPawn* local,
                       C_CSPlayerPawn* target,
                       const Candidate& candidate,
                       const Bones::Capsule& capsule,
                       const Vector_t& eye);
    int AimPoints(C_CSPlayerPawn* local,
                  C_CSPlayerPawn* target,
                  const Candidate& candidate,
                  const Bones::GroupCapsule& box,
                  const Vector_t& eye,
                  const Settings& settings,
                  Vector_t (&points)[5]);
    float RayDamageUpperBound(C_CSWeaponBase* weapon,
                              const Candidate& target,
                              const Vector_t& eye,
                              const Vector_t& ray,
                              const Settings& settings,
                              RayIntersection* intersection = nullptr);
    Candidate ScanCandidate(C_CSPlayerPawn* local,
                            C_CSWeaponBase* weapon,
                            const Candidate& pose,
                            const Vector_t& eye,
                            const QAngle_t& view,
                            const Settings& settings,
                            bool firstEligible = false,
                            bool preparation = false,
                            ScanAcceptance acceptance = {},
                            const Vector_t& offset = {},
                            ScanCursor* cursor = nullptr);
    bool HasDamage(C_CSPlayerPawn* local,
                   C_CSWeaponBase* weapon,
                   const Candidate& pose,
                   const Vector_t& eye,
                   const QAngle_t& view,
                   const Settings& settings,
                   const Vector_t& offset = {},
                   ScanBudget* budget = nullptr);
    Candidate SelectCandidate(C_CSPlayerPawn* local,
                              C_CSWeaponBase* weapon,
                              const Vector_t& eye,
                              const QAngle_t& view,
                              const Settings& settings,
                              CBaseHandle retained = {},
                              bool preparation = false,
                              bool retainedOnly = false,
                              ScanAcceptance acceptance = {},
                              ScanCursor* cursor = nullptr,
                              int forecastTicks = 0,
                              double extrapolationTime = 0.);
    // Reuse only for the same captured candidate, eye, ray and weapon range.
    // This skips geometry only; live health, damage and cover are still checked.
    float RayDamage(C_CSPlayerPawn* local,
                    C_CSWeaponBase* weapon,
                    const Candidate& target,
                    const Vector_t& eye,
                    const Vector_t& ray,
                    const Settings& settings,
                    float minimumDamage = 0.f,
                    const RayIntersection* intersection = nullptr,
                    bool* unobstructed = nullptr);
} // namespace Rage
