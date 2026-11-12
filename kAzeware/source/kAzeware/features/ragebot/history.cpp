#include "history.h"
#include "../legitbot/accuracy.h"
#include "shots.h"
#include "auto_aim.h"
#include "../legitbot/damage.h"
#include "../legitbot/ray.h"
#include "../sdk_prio_a/sdk_prio_a.h"
#include "../gamemode/gamemode.h"
#include "../chams/chams.h"
#include "../../config/config.h"
#include "../../interfaces/interfaces.h"
#include "../../interfaces/CUserCmd/CUserCmd.h"
#include "../../utils/cvar/cvar.h"
#include "../../../cs2/entity/CCSPlayerController/CCSPlayerController.h"
#include "../../../cs2/entity/C_CSWeaponBase/C_CSWeaponBase.h"
#include <numbers>
#include <chrono>

namespace Rage {
    namespace {
        struct Interpolation {
            float fraction = 0;
            int source = 0, destination = 0;
            bool present = false;
        };

        struct ClientInterpolation {
            float fraction = 0;
            bool present = false;
        };

        struct Record {
            Candidate pose{};
            Vector_t origin{}, head{};
            ClientInterpolation client{};
            Interpolation server0{}, server1{};
            float spawnTime = 0, simulationTime = 0;
        };

        struct Track {
            CBaseHandle handle{};
            std::array<Record, 32> records{};
            int next = 0, count = 0;
            float spawnTime = 0;
        };

        std::array<Track, SdkPrioA::kMaxTrackedControllers> tracks{};
        bool recording = false, recordingChams = false;

        struct Evaluation {
            Candidate value{};
            int slot = -1;
        };

        std::array<Evaluation, SdkPrioA::kMaxTrackedControllers> evaluations{};
        int scanCommand = -1, scanCursor = 0, shotScanCursor = 0;
        C_CSPlayerPawn* scanLocal = nullptr;
        C_CSWeaponBase* scanWeapon = nullptr;
        Settings scanSettings{};
        bool haveScanSettings = false;

        void ClearEvaluations() {
            for (auto& e : evaluations) {
                e.value.handle = {};
                e.value.group = -1;
                e.slot = -1;
            }
            scanCommand = -1;
            scanCursor = 0;
            haveScanSettings = false;
        }

        bool SameSelection(const Settings& a, const Settings& b) {
            return a.fov == b.fov && a.priority == b.priority && a.hitboxes == b.hitboxes && a.body_aim == b.body_aim &&
                   a.per_hitbox == b.per_hitbox && a.point_scale == b.point_scale && a.point_scales == b.point_scales &&
                   a.damage_mode == b.damage_mode && a.min_damage == b.min_damage && a.double_tap == b.double_tap &&
                   a.double_tap_ready == b.double_tap_ready && a.auto_multipoint_mode == b.auto_multipoint_mode &&
                   a.lethal_margin == b.lethal_margin && a.autowall == b.autowall && a.backtrack == b.backtrack &&
                   a.history_ms == b.history_ms;
        }

        int Points(const Bones::Capsule& capsule,
                   const Vector_t& eye,
                   const Settings& settings,
                   int group,
                   Vector_t* points,
                   bool centerVisible = false) {
            points[0] = capsule.center;
            if (centerVisible)
                return 1;
            const auto d = capsule.center - eye;
            const float h = std::hypot(d.x, d.y), length = d.Length();
            if (h <= .001f || length <= .001f)
                return 1;
            const Vector_t right{-d.y / h, d.x / h, 0},
                up{-d.z * d.x / (length * h), -d.z * d.y / (length * h), h / length};
            const float scale = settings.PointScale(group);
            const float radius = scale == 0.f ? AutoAim::PointRadius(capsule.radius, length, settings.auto_point_cone)
                                              : capsule.radius * scale * .0095f;
            if (radius <= .001f)
                return 1;
            points[1] = capsule.center + right * radius;
            points[2] = capsule.center - right * radius;
            points[3] = capsule.center + up * radius;
            points[4] = capsule.center - up * radius;
            return 5;
        }

        double shotClockOffset = 1.;

        double Time(const Candidate& c) {
            return double(c.renderTick) + c.renderFraction;
        }

        double RewindTime(const Record& record, const Settings&) {
            return Time(record.pose);
        }

        bool SpawnTime(C_CSPlayerPawn* pawn, float& time) {
            const auto offset = SchemaFinder::Get(hash_32_fnv1a_const("C_CSPlayerPawnBase->m_flLastSpawnTimeIndex"));
            return offset && Mem::ReadField(pawn, offset, time) && std::isfinite(time);
        }

        bool WithinHistoryWindow(const Candidate& c, double shotTime, double maximum) {
            const double age = (shotTime - Time(c)) / 64.;
            return std::isfinite(c.renderFraction) && c.renderFraction >= 0.f && c.renderFraction < 1.f &&
                   std::isfinite(age) && age >= 0. && age <= maximum && c.renderTick > 0;
        }

        int OldestValidRecord(const Track& track, double shotTime, double maximum) {
            for (int i = 0; i < track.count; ++i) {
                const int slot = (track.next + 32 - track.count + i) % 32;
                if (WithinHistoryWindow(track.records[slot].pose, shotTime, maximum))
                    return slot;
            }
            return -1;
        }

        bool Valid(const Candidate& c, const Settings& settings, double shotTime) {
            if (!settings.backtrack || settings.history_ms <= 0 || !Cvar::Bool("sv_unlag", true))
                return false;
            // Simulation poses add the native render lead on submission, so their
            // stored time already measures the geometry's age without another tick.
            const double rewindLead =
                !c.simulationPose && (settings.SeedGate() || settings.force_no_spread) ? 1. / 64. : 0.;
            const double maximum =
                (std::min)(settings.history_ms * .001, double(Cvar::Float("sv_maxunlag", .2f)) - rewindLead);
            return WithinHistoryWindow(c, shotTime, maximum);
        }

        Interpolation Read(const CCSGOInterpolationInfoPB* p) {
            return p ? Interpolation{(p->nCachedBits & 1) ? p->flFraction : 0.f,
                                     (p->nCachedBits & 2) ? p->nSrcTick : -1,
                                     (p->nCachedBits & 4) ? p->nDstTick : -1,
                                     true}
                     : Interpolation{};
        }

        void Write(CCSGOInterpolationInfoPB* p, const Interpolation& value) {
            if (!p || !value.present)
                return;
            p->flFraction = value.fraction;
            p->nSrcTick = value.source;
            p->nDstTick = value.destination;
            p->SetBits(7);
        }

        ClientInterpolation ReadClientInterpolation(const CCSGOInputHistoryEntryPB& entry) {
            const auto* client = entry.cl_interp;
            if (!(entry.nCachedBits & INPUT_HISTORY_BITS_CL_INTERP))
                return {};
            return {client && (client->nCachedBits & 1) ? client->flFraction : 0.f, true};
        }

        void WriteClientInterpolation(CCSGOInputHistoryEntryPB& entry, const ClientInterpolation& value) {
            if (!value.present) {
                entry.nCachedBits &= ~std::uint64_t(INPUT_HISTORY_BITS_CL_INTERP);
                return;
            }
            entry.cl_interp->flFraction = value.fraction;
            entry.cl_interp->SetBits(1);
            entry.SetBits(INPUT_HISTORY_BITS_CL_INTERP);
        }

        const Record* FindRecord(const Candidate& target) {
            for (const auto& track : tracks)
                if (track.handle == target.handle)
                    for (int i = 0; i < track.count; ++i) {
                        const auto& r = track.records[(track.next + 31 - i) % 32];
                        if (r.pose.renderTick == target.renderTick && r.pose.renderFraction == target.renderFraction &&
                            r.pose.simulationPose == target.simulationPose)
                            return &r;
                    }
            return nullptr;
        }
    } // namespace

    void BindPoseInterpolation(Candidate& pose, const CCSGOInputHistoryEntryPB& entry) {
        const auto client = ReadClientInterpolation(entry);
        const auto valid = [](const CCSGOInterpolationInfoPB* value) {
            const auto wire = Read(value);
            return wire.present && wire.source >= 0 && wire.destination >= wire.source &&
                   std::isfinite(wire.fraction) && wire.fraction >= 0.f && wire.fraction <= 1.f;
        };
        // A valid native interpolation tuple already describes the rendered pose.
        // Without it, copied bones belong to the captured entity simulation tick.
        if (client.present && std::isfinite(client.fraction) && client.fraction >= 0.f && client.fraction <= 1.f &&
            (entry.nCachedBits & INPUT_HISTORY_BITS_SV_INTERP0) &&
            (entry.nCachedBits & INPUT_HISTORY_BITS_SV_INTERP1) && valid(entry.sv_interp0) && valid(entry.sv_interp1)) {
            pose.renderTick = entry.nRenderTickCount;
            pose.renderFraction = entry.flRenderTickFraction;
            pose.simulationPose = false;
        }
    }

    void ResetHistory() {
        if (recordingChams) {
            chams::ClearHistorySelection();
            recordingChams = false;
        }
        for (auto& t : tracks) {
            t.handle = {};
            t.next = t.count = 0;
        }
        recording = false;
        shotClockOffset = 1.;
        shotScanCursor = 0;
        ClearEvaluations();
    }

    void ObserveShotClock(int clientTick, double shotTime) {
        if (clientTick > 0 && std::isfinite(shotTime))
            shotClockOffset = shotTime - clientTick;
    }

    double ForecastShotClock(int clientTick) {
        return clientTick + shotClockOffset;
    }

    void CaptureHistory(CUserCmd* cmd, C_CSPlayerPawn* local, const Settings& settings) {
        if (!settings.backtrack || settings.history_ms <= 0) {
            if (recording)
                ResetHistory();
            return;
        }
        auto* sample = cmd ? cmd->GetInputHistoryEntry(cmd->csgoUserCmd.inputHistoryField.nCurrentSize - 1) : nullptr;
        if (!sample || sample->nRenderTickCount <= 0 || !std::isfinite(sample->flRenderTickFraction) ||
            sample->flRenderTickFraction < 0.f || sample->flRenderTickFraction >= 1.f || !I::GameEntity ||
            !I::GameEntity->Instance)
            return;
        const bool captureChams =
            Config::enemy_visuals.enabled && Config::enemyHistoryChams && Cvar::Bool("sv_unlag", true);
        if (!captureChams && recordingChams)
            chams::ClearHistorySelection();
        recordingChams = captureChams;
        const bool filterChamsTeam = captureChams && GameMode::WantTeamCheck(true);
        recording = true;
        int controllers[SdkPrioA::kMaxTrackedControllers]{};
        const int count = SdkPrioA::CopyControllerIndices(controllers, SdkPrioA::kMaxTrackedControllers);
        std::array<bool, SdkPrioA::kMaxTrackedControllers> seen{};
        std::uint64_t snapshotEnemies = 0;
        for (int i = 0; i < count; ++i) {
            auto* controller = I::GameEntity->Instance->Get<CCSPlayerController>(controllers[i]);
            if (!controller || !controller->m_bPawnIsAlive())
                continue;
            const auto handle = controller->m_hPlayerPawn();
            auto* pawn = I::GameEntity->Instance->Get<C_CSPlayerPawn>(handle);
            if (!pawn || pawn == local || pawn->m_iHealth() <= 0 || pawn->m_lifeState() != 0 ||
                pawn->m_bGunGameImmunity())
                continue;
            auto* node = pawn->m_pGameSceneNode();
            if (!node || node->m_bDormant())
                continue;
            int slot = -1;
            for (int j = 0; j < int(tracks.size()); ++j)
                if (tracks[j].handle == handle) {
                    slot = j;
                    break;
                }
            if (slot < 0)
                for (int j = 0; j < int(tracks.size()); ++j)
                    if (!tracks[j].handle.valid()) {
                        slot = j;
                        break;
                    }
            if (slot < 0)
                continue;
            auto& track = tracks[slot];
            seen[slot] = true;
            track.handle = handle;
            const bool captureEnemy = captureChams && (!filterChamsTeam || pawn->m_iTeamNum() != local->m_iTeamNum());
            if (captureEnemy)
                snapshotEnemies |= std::uint64_t{1} << slot;
            float spawnTime = 0;
            if (!SpawnTime(pawn, spawnTime)) {
                track.next = track.count = 0;
                evaluations[slot] = {};
                continue;
            }
            if (track.count && track.spawnTime != spawnTime) {
                track.next = track.count = 0;
                evaluations[slot] = {};
            }
            track.spawnTime = spawnTime;
            const auto origin = node->m_vecAbsOrigin();
            const float simulation = pawn->m_flSimulationTime();
            const double poseTime = double(simulation) * 64.;
            if (!std::isfinite(poseTime) || poseTime <= 0. || poseTime >= INT_MAX - 1)
                continue;
            Candidate captured{};
            captured.renderTick = int(poseTime);
            captured.renderFraction = float(poseTime - captured.renderTick);
            captured.simulationPose = true;
            BindPoseInterpolation(captured, *sample);
            if (track.count) {
                const auto& previous = track.records[(track.next + 31) % 32];
                const double time = Time(captured);
                if (time < Time(previous.pose) || (origin - previous.origin).Length() > 64.f) {
                    track.next = track.count = 0;
                    evaluations[slot] = {};
                }
                // One record per rendered tick, but keep its most recent fraction
                // and matching pose instead of freezing the first frame of a tick.
                else if (captured.renderTick == previous.pose.renderTick) {
                    if (time == Time(previous.pose))
                        continue;
                    track.next = (track.next + 31) % 32;
                    --track.count;
                    evaluations[slot] = {};
                }
            }
            Record record{};
            record.spawnTime = spawnTime;
            record.origin = origin;
            record.pose = captured;
            record.pose.handle = handle;
            record.pose.historical = true;
            record.simulationTime = simulation;
            record.pose.seedInset = SeedPoseInset(pawn->m_vecVelocity());
            record.pose.boxCount = Bones::CollectHitboxCapsulesReadonly(
                pawn, 0xff, record.pose.boxes.data(), int(record.pose.boxes.size()));
            if (!record.pose.boxCount || pawn->m_flSimulationTime() != simulation)
                continue;
            for (int j = 0; j < record.pose.boxCount; ++j)
                if (record.pose.boxes[j].group == 0) {
                    record.head = record.pose.boxes[j].capsule.center;
                    break;
                }
            if (!record.pose.simulationPose) {
                record.client = ReadClientInterpolation(*sample);
                record.server0 = Read(sample->sv_interp0);
                record.server1 = Read(sample->sv_interp1);
            }
            if (captureEnemy)
                chams::CaptureHistoryPose(slot, track.next, pawn, handle);
            track.records[track.next] = record;
            track.next = (track.next + 1) % 32;
            track.count = (std::min)(32, track.count + 1);
        }
        for (int i = 0; i < int(tracks.size()); ++i)
            if (!seen[i]) {
                tracks[i].handle = {};
                tracks[i].next = tracks[i].count = 0;
            }
        if (captureChams) {
            const auto* base = cmd->csgoUserCmd.pBaseCmd;
            double shotTime = base ? ForecastShotClock(base->nClientTick) : 0.;
            // Use the same mapped player time as this command's native shot.
            Legitbot::ReadShotClock(cmd, local->GetActiveWeapon(), shotTime);
            const double maximum = (std::min)(settings.history_ms * .001, double(Cvar::Float("sv_maxunlag", .2f)));
            for (int i = 0; i < int(tracks.size()); ++i) {
                const auto& track = tracks[i];
                const int oldest =
                    (snapshotEnemies & (std::uint64_t{1} << i)) ? OldestValidRecord(track, shotTime, maximum) : -1;
                chams::SelectHistoryPose(i, oldest, track.handle);
            }
        }
    }

    Candidate SelectHistory(CUserCmd* cmd,
                            C_CSPlayerPawn* local,
                            C_CSWeaponBase* weapon,
                            const Vector_t& eye,
                            const QAngle_t& view,
                            const Settings& configured,
                            CBaseHandle retained,
                            double shotTime) {
        auto settings = configured;
        if (settings.AutoMultipoint() && settings.auto_point_cone < 0.f)
            settings.auto_point_cone = AutoPointCone(
                Legitbot::ReadFireAccuracy(local, weapon), settings.auto_multipoint_mode, settings.hc_budget);
        Candidate best{};
        const unsigned mask = settings.HitboxMask();
        if (!mask || !settings.backtrack || !recording)
            return best;
        if (scanLocal != local || scanWeapon != weapon || !haveScanSettings || !SameSelection(scanSettings, settings)) {
            ClearEvaluations();
            scanLocal = local;
            scanWeapon = weapon;
            scanSettings = settings;
            haveScanSettings = true;
        }
        const bool teamFilter = GameMode::WantTeamCheck(true);
        // Cached candidates are proposals only. Reproject the multipoint for the
        // current eye and recheck world damage, health and history before reuse.
        for (int i = 0; i < int(tracks.size()); ++i) {
            const auto& track = tracks[i];
            auto& cached = evaluations[i];
            auto& candidate = cached.value;
            if (!candidate)
                continue;
            auto* pawn = I::GameEntity->Instance->Get<C_CSPlayerPawn>(track.handle);
            bool valid = pawn && track.handle == candidate.handle && cached.slot >= 0 && pawn->m_iHealth() > 0 &&
                         pawn->m_lifeState() == 0 && !pawn->m_bGunGameImmunity() &&
                         (!teamFilter || pawn->m_iTeamNum() != local->m_iTeamNum());
            if (valid) {
                const auto& record = track.records[cached.slot];
                valid = (track.next + 31 - cached.slot) % 32 < track.count &&
                        record.pose.renderTick == candidate.renderTick &&
                        record.pose.renderFraction == candidate.renderFraction && Valid(candidate, settings, shotTime);
                if (valid) {
                    Vector_t points[5];
                    bool found = false;
                    float checkedDamage = -1.f;
                    for (int j = 0; j < record.pose.boxCount; ++j) {
                        const auto& box = record.pose.boxes[j];
                        if (box.capsule.studioIndex != candidate.studio)
                            continue;
                        bool center = CenterVisible(local, pawn, candidate, box.capsule, eye);
                        if (!center && RayStudio(candidate,
                                                 eye,
                                                 (box.capsule.center - eye).Normalize(),
                                                 weapon->Data()->m_flRange()) == candidate.studio) {
                            checkedDamage = RayDamage(local,
                                                      weapon,
                                                      candidate,
                                                      eye,
                                                      (box.capsule.center - eye).Normalize(),
                                                      settings,
                                                      float(settings.Minimum(pawn->m_iHealth())));
                            center = checkedDamage >= settings.Minimum(pawn->m_iHealth());
                            if (!center)
                                checkedDamage = -1.f;
                        }
                        if (center)
                            candidate.pointIndex = 0;
                        const int n = Points(box.capsule, eye, settings, box.group, points, center);
                        if (candidate.pointIndex >= 0 && candidate.pointIndex < n) {
                            candidate.point = points[candidate.pointIndex];
                            found = true;
                        }
                        break;
                    }
                    valid = found && InAimCone(candidate.point - eye, view, settings.fov);
                    if (valid)
                        valid = RayStudio(
                                    candidate, eye, (candidate.point - eye).Normalize(), weapon->Data()->m_flRange()) ==
                                candidate.studio;
                    if (valid) {
                        candidate.health = pawn->m_iHealth();
                        candidate.distance = (record.origin - eye).Length();
                        candidate.damage = checkedDamage >= 0.f ? checkedDamage
                                                                : RayDamage(local,
                                                                            weapon,
                                                                            candidate,
                                                                            eye,
                                                                            (candidate.point - eye).Normalize(),
                                                                            settings,
                                                                            float(settings.Minimum(candidate.health)));
                        valid = candidate.damage >= settings.Minimum(candidate.health);
                        if (valid) {
                            Vector_t forward;
                            view.ToDirections(&forward);
                            candidate.angle =
                                std::acos(std::clamp(
                                    Legitbot::Dot(forward, (candidate.point - eye).Normalize()), -1.f, 1.f)) *
                                (180.f / std::numbers::pi_v<float>);
                        }
                    }
                }
            }
            if (!valid) {
                candidate.handle = {};
                candidate.group = -1;
                cached.slot = -1;
            }
        }
        // Share a bounded round-robin scan across preparation and firing in the
        // same command. All record slots remain eligible; no enemy is pinned out.
        if (scanCommand != cmd->nCommandNumber) {
            scanCommand = cmd->nCommandNumber;
            ScanBudget budget{std::chrono::steady_clock::now() + std::chrono::microseconds(750)};
            const int total = int(tracks.size()) * 32;
            for (int visited = 0; visited < total && !budget.Expired(); ++visited) {
                const int task = scanCursor;
                scanCursor = (scanCursor + 1) % total;
                const int ti = task % int(tracks.size()), slot = task / int(tracks.size());
                const auto& track = tracks[ti];
                if (!track.handle.valid() || !track.count || (track.next + 31 - slot) % 32 >= track.count)
                    continue;
                auto* pawn = I::GameEntity->Instance->Get<C_CSPlayerPawn>(track.handle);
                if (!pawn || pawn->m_iHealth() <= 0 || pawn->m_lifeState() != 0 || pawn->m_bGunGameImmunity() ||
                    (teamFilter && pawn->m_iTeamNum() == local->m_iTeamNum()))
                    continue;
                const auto& record = track.records[slot];
                if (record.pose.handle != track.handle || !Valid(record.pose, settings, shotTime))
                    continue;
                auto pose = record.pose;
                pose.health = pawn->m_iHealth();
                pose.distance = (record.origin - eye).Length();
                const auto candidate = ScanCandidate(
                    local, weapon, pose, eye, view, settings, false, false, {nullptr, nullptr, 0.f, &budget});

                auto& cached = evaluations[ti];
                if ((cached.slot == slot && cached.value.renderTick == candidate.renderTick) ||
                    Prefer(candidate, cached.value, settings, retained)) {
                    cached.value = candidate;
                    cached.slot = candidate ? slot : -1;
                }
            }
        }
        for (const auto& cached : evaluations)
            if (Prefer(cached.value, best, settings, retained))
                best = cached.value;
        return best;
    }

    static bool
    CanApplyRecord(CUserCmd* cmd, int index, const Record& saved, const Settings& settings, double shotTime) {
        const auto& target = saved.pose;
        if (!target.historical)
            return true;
        if (!Valid(target, settings, shotTime))
            return false;
        auto* entry = cmd->GetInputHistoryEntry(index);
        if (!entry)
            return false;
        const Record* record = &saved;
        const double rewind = RewindTime(*record, settings);
        if (!std::isfinite(rewind) || rewind <= 0. || rewind >= INT_MAX - 1 || rewind > shotTime)
            return false;
        auto* pawn = I::GameEntity && I::GameEntity->Instance
                         ? I::GameEntity->Instance->Get<C_CSPlayerPawn>(target.handle)
                         : nullptr;
        if (!pawn || pawn->m_iHealth() <= 0 || pawn->m_lifeState() != 0 || pawn->m_bGunGameImmunity())
            return false;
        float spawnTime = 0;
        if (!SpawnTime(pawn, spawnTime) || spawnTime != record->spawnTime)
            return false;
        auto* node = pawn->m_pGameSceneNode();
        if (!node || node->m_bDormant())
            return false;
        return !(record->client.present && !entry->cl_interp) && !(record->server0.present && !entry->sv_interp0) &&
               !(record->server1.present && !entry->sv_interp1);
    }

    Candidate SelectShootableHistory(CUserCmd* cmd,
                                     int index,
                                     C_CSPlayerPawn* local,
                                     C_CSWeaponBase* weapon,
                                     const Vector_t& eye,
                                     const QAngle_t& view,
                                     const Settings& settings,
                                     CBaseHandle retained,
                                     double shotTime,
                                     const Candidate& live,
                                     ScanAcceptance acceptance) {
        if (!recording || !settings.backtrack || !settings.HitboxMask())
            return {};
        auto* data = weapon->Data();
        if (!data)
            return {};
        ScanBudget budget{std::chrono::steady_clock::now() + std::chrono::microseconds(750)};
        if (acceptance.budget) {
            if (acceptance.budget->Expired())
                return {};
            budget.deadline = (std::min)(budget.deadline, acceptance.budget->deadline);
        }
        Candidate best{};

        struct RankedAcceptance {
            ScanAcceptance original;
            const Candidate& live;
            const Candidate& best;
            const Settings& settings;
            CBaseHandle retained;
        } ranked{acceptance, live, best, settings, retained};

        ScanAcceptance filter{
            [](void* context, const Candidate& candidate) {
                const auto& rank = *static_cast<RankedAcceptance*>(context);
                return Prefer(candidate, rank.best ? rank.best : rank.live, rank.settings, rank.retained) &&
                       rank.original.test(rank.original.context, candidate);
            },
            &ranked,
            acceptance.damageFloor,
            &budget};
        const bool teamFilter = GameMode::WantTeamCheck(true);
        const int total = int(tracks.size()) * 32;
        // Advance even on a rejected record so a difficult point cannot pin history.
        int cursor = shotScanCursor;
        for (int visited = 0; visited < total && !budget.Expired(); ++visited) {
            const int task = cursor;
            cursor = (cursor + 1) % total;
            const int slot = task / int(tracks.size());
            const auto& track = tracks[task % int(tracks.size())];
            if (!track.handle.valid() || !track.count || (track.next + 31 - slot) % 32 >= track.count)
                continue;
            const auto& record = track.records[slot];
            if (record.pose.handle != track.handle || !CanApplyRecord(cmd, index, record, settings, shotTime))
                continue;
            auto* pawn = I::GameEntity->Instance->Get<C_CSPlayerPawn>(track.handle);
            if (teamFilter && pawn->m_iTeamNum() == local->m_iTeamNum())
                continue;
            const auto& incumbent = best ? best : live;
            if (incumbent && settings.priority == 1 && data->m_nNumBullets() == 1)
                filter.damageFloor = (std::max)(acceptance.damageFloor, incumbent.damage - .01f);
            auto pose = record.pose;
            pose.distance = (record.origin - eye).Length();
            const auto candidate = ScanCandidate(local, weapon, pose, eye, view, settings, false, false, filter);
            if (candidate)
                best = candidate;
        }
        shotScanCursor = cursor;
        return best;
    }

    bool CanApplyHistory(CUserCmd* cmd, int index, const Candidate& target, const Settings& settings, double shotTime) {
        if (target.extrapolated) {
            auto* pawn = I::GameEntity && I::GameEntity->Instance
                             ? I::GameEntity->Instance->Get<C_CSPlayerPawn>(target.handle)
                             : nullptr;
            const double advance = Time(target) - double(target.sourceSimulationTime) * 64.;
            // A new packet invalidates this forecast. Its timestamp and geometry
            // must stay together through commit/replay, including a DT second shot.
            return settings.extrapolation && !target.historical && pawn && pawn->m_iHealth() > 0 &&
                   pawn->m_flSimulationTime() == target.sourceSimulationTime && advance >= 1. &&
                   advance <= settings.extrapolation_ticks && Time(target) <= shotTime;
        }
        if (!target.historical)
            return true;
        const auto* record = FindRecord(target);
        return record && CanApplyRecord(cmd, index, *record, settings, shotTime);
    }

    bool ApplyHistory(CUserCmd* cmd, int index, const Candidate& target, const Settings& settings, double shotTime) {
        if (!target.historical && target.renderTick <= 0)
            return true;
        if (!CanApplyHistory(cmd, index, target, settings, shotTime))
            return false;
        auto* entry = cmd->GetInputHistoryEntry(index);
        if (!entry)
            return false;
        const auto* record = target.historical ? FindRecord(target) : nullptr;
        const double time = record ? RewindTime(*record, settings) : Time(target);
        const int tick = int(time);
        const int renderLead = target.simulationPose && tick < INT_MAX ? 1 : 0;
        entry->nRenderTickCount = tick + renderLead;
        entry->flRenderTickFraction = float(time - tick);
        entry->SetBits(INPUT_HISTORY_BITS_RENDERTICKCOUNT | INPUT_HISTORY_BITS_RENDERTICKFRACTION);
        if (target.simulationPose)
            entry->nCachedBits &= ~std::uint64_t(INPUT_HISTORY_BITS_CL_INTERP);
        if (target.extrapolated)
            entry->nCachedBits &= ~std::uint64_t(INPUT_HISTORY_BITS_SV_INTERP0 | INPUT_HISTORY_BITS_SV_INTERP1);
        if (!target.historical)
            return true;
        entry->nTargetEntIndex = target.handle.index();
        entry->SetBits(INPUT_HISTORY_BITS_TARGETENTINDEX);
        WriteClientInterpolation(*entry, target.simulationPose ? ClientInterpolation{} : record->client);
        Write(entry->sv_interp0, record->server0);
        Write(entry->sv_interp1, record->server1);
        if (record->server0.present)
            entry->SetBits(INPUT_HISTORY_BITS_SV_INTERP0);
        if (record->server1.present)
            entry->SetBits(INPUT_HISTORY_BITS_SV_INTERP1);
        if (entry->pTargetHeadPositionCheck) {
            entry->pTargetHeadPositionCheck->vecValue = {record->head.x, record->head.y, record->head.z, 0};
            entry->pTargetHeadPositionCheck->SetBits(7);
            entry->SetBits(INPUT_HISTORY_BITS_TARGETHEADPOSITIONCHECK);
        }
        if (entry->pTargetAbsPositionCheck) {
            entry->pTargetAbsPositionCheck->vecValue = {record->origin.x, record->origin.y, record->origin.z, 0};
            entry->pTargetAbsPositionCheck->SetBits(7);
            entry->SetBits(INPUT_HISTORY_BITS_TARGETABSPOSITIONCHECK);
        }
        entry->nCachedBits &= ~std::uint64_t(INPUT_HISTORY_BITS_TARGETANGCHECK);
        return true;
    }
} // namespace Rage
