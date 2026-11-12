#include "anti_aim.h"
#include "angles.h"
#include "../sdk_prio_a/sdk_prio_a.h"
#include "../gamemode/gamemode.h"
#include "../../interfaces/CUserCmd/CUserCmd.h"
#include "../../interfaces/interfaces.h"
#include "../../../cs2/entity/CCSPlayerController/CCSPlayerController.h"
#include <numbers>
#include "../legitbot/accuracy.h"
#include "../bones/bones.h"
#include "../nadepred/nadepred.h"
#include <limits>

namespace Rage {
    namespace {
        struct RollCorrection {
            CBaseHandle pawn{};
            float spawnTime = 0.f;
            int command = 0;
            float offset = 0.f;
        } rollCorrection;

        float DirectionBasis(C_CSPlayerPawn* local, const Settings& c, const QAngle_t& view, CBaseHandle selected) {
            if (!c.yaw_basis || !I::GameEntity || !I::GameEntity->Instance)
                return view.y;
            const auto eye = Bones::GetEyePos(local);
            const bool teamCheck = GameMode::WantTeamCheck(true);
            const auto eligible = [&](C_CSPlayerPawn* pawn) {
                return pawn && pawn != local && pawn->m_iHealth() > 0 && pawn->m_lifeState() == 0 &&
                       !pawn->m_bGunGameImmunity() && (!teamCheck || pawn->m_iTeamNum() != local->m_iTeamNum()) &&
                       pawn->m_pGameSceneNode() && !pawn->m_pGameSceneNode()->m_bDormant();
            };
            C_CSPlayerPawn* target = nullptr;
            if (c.yaw_basis == 1) {
                if (selected.valid())
                    target = I::GameEntity->Instance->Get<C_CSPlayerPawn>(selected);
                if (!eligible(target))
                    target = nullptr;
            } else {
                int indices[SdkPrioA::kMaxTrackedControllers]{};
                const int count = SdkPrioA::CopyControllerIndices(indices, SdkPrioA::kMaxTrackedControllers);
                Vector_t forward;
                view.ToDirections(&forward);
                float best = -2.f;
                for (int i = 0; i < count; ++i) {
                    auto* controller = I::GameEntity->Instance->Get<CCSPlayerController>(indices[i]);
                    if (!controller || !controller->m_bPawnIsAlive())
                        continue;
                    auto* pawn = I::GameEntity->Instance->Get<C_CSPlayerPawn>(controller->m_hPlayerPawn());
                    if (!eligible(pawn))
                        continue;
                    const auto delta = (Bones::GetEyePos(pawn) - eye).Normalize();
                    const float dot = delta.x * forward.x + delta.y * forward.y + delta.z * forward.z;
                    if (dot > best) {
                        best = dot;
                        target = pawn;
                    }
                }
            }
            if (!target)
                return view.y;
            const auto delta = Bones::GetEyePos(target) - eye;
            return std::atan2(delta.y, delta.x) * (180.f / std::numbers::pi_v<float>);
        }

        float ModelRollOffset(C_CSPlayerPawn* local, int command, bool grounded) {
            static std::uint32_t spawnOffset = 0;
            if (!spawnOffset)
                spawnOffset = SchemaFinder::Get(hash_32_fnv1a_const("C_CSPlayerPawnBase->m_flLastSpawnTimeIndex"));
            float spawnTime = 0.f;
            const auto pawn = local->handle();
            if (!pawn.valid() || !spawnOffset || !Mem::ReadField(local, spawnOffset, spawnTime) ||
                !std::isfinite(spawnTime)) {
                rollCorrection = {};
                return 0.f;
            }
            auto& roll = rollCorrection;
            if (roll.pawn != pawn || roll.spawnTime != spawnTime || command < roll.command ||
                command - roll.command > 64)
                roll = {pawn, spawnTime};
            // A jump animates the torso independently of body yaw. Keep the last
            // grounded correction throughout the flight, including crouch changes.
            if (!grounded) {
                if (roll.command)
                    roll.command = command;
                return roll.offset;
            }
            const auto eyeOffset = SchemaFinder::Get(hash_32_fnv1a_const("C_CSPlayerPawn->m_angEyeAngles"));
            QAngle_t eye{};
            if (!eyeOffset || !Mem::ReadField(local, eyeOffset, eye) || std::abs(eye.x) < .1f)
                return 0.f;
            Vector_t positions[Bones::S_COUNT]{};
            bool valid[Bones::S_COUNT]{};
            Bones::CollectSkeletonPoints(local, positions, valid, false);
            if (!valid[Bones::S_HEAD] || !valid[Bones::S_PELVIS])
                return 0.f;
            auto* node = local->m_pGameSceneNode();
            const auto rootOffset = SchemaFinder::Get(hash_32_fnv1a_const("CGameSceneNode->m_angAbsRotation"));
            QAngle_t root{};
            if (!node || !rootOffset || !Mem::ReadField(node, rootOffset, root) || !root.IsValid())
                return 0.f;
            auto pelvis = positions[Bones::S_PELVIS];
            const auto origin = node->m_vecAbsOrigin();
            // The generic skeleton map can use the floor/root joint as its pelvis
            // fallback. Roll needs a torso anchor, not that root translation.
            if (pelvis.z - origin.z < (positions[Bones::S_HEAD].z - origin.z) * .2f && valid[Bones::S_SPINE0])
                pelvis = positions[Bones::S_SPINE0];
            const auto lean = positions[Bones::S_HEAD] - pelvis;
            if (std::hypot(lean.x, lean.y) < .01f)
                return 0.f;
            // Compare the lean with its rendered root, not the newer command eye yaw.
            // Use/landing animations lag that command; feeding their yaw lag back into
            // the next command produced a self-sustaining turn oscillation.
            const float measured = std::remainder(
                std::atan2(lean.y, lean.x) * (180.f / std::numbers::pi_v<float>)-root.y - (eye.x < 0.f ? 180.f : 0.f),
                360.f);
            // The pose is a delayed response to our own command. Applying its whole
            // error on every tick gives that feedback loop unit gain and sustains
            // alternating yaw after use/landing. Filter the relative offset, never
            // the requested yaw, so camera/manual direction changes remain immediate.
            if (!roll.command)
                roll.offset = measured;
            else if (command != roll.command)
                roll.offset =
                    std::remainder(roll.offset + std::remainder(measured - roll.offset, 360.f) * .1175031f, 360.f);
            roll.command = command;
            return roll.offset;
        }

        float Pitch(const Settings& c, int command, float view) {
            float lo = (std::min)(c.pitch_low, c.pitch_high), hi = (std::max)(c.pitch_low, c.pitch_high);
            switch (c.pitch_mode) {
            case 1:
                return -88.5f;
            case 2:
                return 88.5f;
            case 3:
                return c.pitch;
            case 4:
                return command & 1 ? lo : hi;
            case 5: {
                std::uint32_t random = std::uint32_t(command) * 747796405u + 2891336453u;
                random = ((random >> ((random >> 28) + 4)) ^ random) * 277803737u;
                random = (random >> 22) ^ random;
                return lo + (hi - lo) * (random / 4294967295.f);
            }
            case 6: {
                const float span = hi - lo;
                if (span < .001f)
                    return lo;
                const float phase = std::fmod(float(command) * 4.5f, span * 2.f);
                return lo + (phase < span ? phase : span * 2.f - phase);
            }
            case 7:
                return command % 3 == 0 ? lo : command % 3 == 1 ? (lo + hi) * .5f : hi;
            default:
                return view;
            }
        }

        bool KnifeThreat(C_CSPlayerPawn* local, float& yaw) {
            int indices[SdkPrioA::kMaxTrackedControllers]{};
            const int count = SdkPrioA::CopyControllerIndices(indices, SdkPrioA::kMaxTrackedControllers);
            float nearest = 350.f;
            bool found = false;
            const auto origin = local->m_pGameSceneNode()->m_vecAbsOrigin();
            for (int i = 0; i < count; ++i) {
                auto* controller = I::GameEntity->Instance->Get<CCSPlayerController>(indices[i]);
                if (!controller || !controller->m_bPawnIsAlive())
                    continue;
                auto* pawn = I::GameEntity->Instance->Get<C_CSPlayerPawn>(controller->m_hPlayerPawn());
                if (!pawn || pawn == local || pawn->m_iHealth() <= 0 ||
                    (GameMode::WantTeamCheck(true) && pawn->m_iTeamNum() == local->m_iTeamNum()))
                    continue;
                auto* node = pawn->m_pGameSceneNode();
                auto* weapon = pawn->GetActiveWeapon();
                auto* data = weapon ? weapon->Data() : nullptr;
                if (!node || node->m_bDormant() || !data || data->m_WeaponType() != 0)
                    continue;
                const auto delta = node->m_vecAbsOrigin() - origin;
                const float distance = std::hypot(delta.x, delta.y);
                if (distance < nearest) {
                    nearest = distance;
                    yaw = std::atan2(delta.y, delta.x) * (180.f / std::numbers::pi_v<float>);
                    found = true;
                }
            }
            return found;
        }
    } // namespace

    void ApplyAntiAim(CUserCmd* cmd,
                      C_CSPlayerPawn* local,
                      const Settings& c,
                      const QAngle_t& realView,
                      CBaseHandle selected,
                      bool generated) {
        const bool yawEnabled = c.yaw_mode || c.avoid_backstab;
        if (!c.anti_aim || !c.roll_correction || !yawEnabled)
            rollCorrection = {};
        if (!c.anti_aim || !cmd || !local || !local->m_pGameSceneNode() || (!c.pitch_mode && !yawEnabled))
            return;
        if (cmd->nButtons.nValue & IN_USE)
            return;
        if (NadePred::ControlsThrow(local->GetActiveWeapon()))
            return;
        if (!(c.silent && c.hide_shots)) {
            auto* weapon = local->GetActiveWeapon();
            auto* data = weapon ? weapon->Data() : nullptr;
            const bool secondary = (cmd->nButtons.nValue & IN_SECOND_ATTACK) && data &&
                                   (data->m_WeaponType() == 0 || weapon->m_iItemDefinitionIndex() == 64);
            const bool primary = (cmd->nButtons.nValue & IN_ATTACK) != 0;
            if (weapon && data && (primary || secondary) && !weapon->m_bInReload() &&
                (data->m_WeaponType() == 0 || weapon->m_iClip1() > 0)) {
                Legitbot::SeedShot shot{};
                if (Legitbot::ReadSeedShotAt(cmd, local, weapon, -1, secondary, shot)) {
                    const double now = double(shot.tick) + shot.fraction;
                    const double next =
                        double(secondary ? weapon->m_nNextSecondaryAttackTick() : weapon->m_nNextPrimaryAttackTick()) +
                        (secondary ? weapon->m_flNextSecondaryAttackTickRatio()
                                   : weapon->m_flNextPrimaryAttackTickRatio()) +
                        weapon->m_flWatTickOffset();
                    const int ready = weapon->m_nPostponeFireReadyTicks();
                    const bool charged = weapon->m_iItemDefinitionIndex() != 64 || secondary ||
                                         (ready > 0 && ready != (std::numeric_limits<int>::max)() &&
                                          now >= double(ready) + weapon->m_flPostponeFireReadyFrac());
                    if (now >= next && charged)
                        return;
                }
            }
        }
        const auto flags = local->m_fFlags();
        if (flags & FL_FROZEN)
            return;
        const auto moveOffset = SchemaFinder::Get(hash_32_fnv1a_const("C_BaseEntity->m_nActualMoveType"));
        if (moveOffset) {
            const auto type = *reinterpret_cast<unsigned char*>(reinterpret_cast<std::uintptr_t>(local) + moveOffset);
            if (type == MOVETYPE_LADDER || type == MOVETYPE_NOCLIP || type == MOVETYPE_OBSERVER)
                return;
        }
        QAngle_t angle = realView;
        angle.x = Pitch(c, cmd->nCommandNumber, realView.x);
        if (yawEnabled)
            angle.y = DirectionBasis(local, c, realView, selected);
        bool knife = c.avoid_backstab && yawEnabled && KnifeThreat(local, angle.y);
        if (!knife) {
            if (c.yaw_mode == 1)
                angle.y += c.yaw;
        }
        if (c.roll_correction && yawEnabled)
            angle.y -= ModelRollOffset(local, cmd->nCommandNumber, (flags & FL_ONGROUND) != 0);
        angle.Normalize();
        angle.x = std::clamp(angle.x, -89.f, 89.f);
        angle.z = 0;
        // Original kAzeware angle modes, with subtick movement-space correction
        // instead of rotating fractional axes that the server quantizes away.
        SetCommandAngle(cmd, angle, local, generated);
    }
} // namespace Rage
