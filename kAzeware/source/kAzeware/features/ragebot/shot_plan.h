#pragma once
#include <algorithm>
#include <array>

namespace Rage {
    template <class State, class Hit> struct ShotPlan {
        int brakeStart = -1, fireTick = -1;
        float overshoot = 0.f;
        State state{};
        Hit hit{};
    };

    // All trajectories start from the same unmodified snapshot. Step implements
    // the control/physics model; query checks MD, HC and the selected stop mode.
    // A late brake candidate can win only by producing an earlier feasible shot.
    template <class State, class Hit, class Step, class Query, class Damage, class Rest, class Distance, class Defer>
    ShotPlan<State, Hit> PlanShot(const State& initial,
                                  int horizon,
                                  int readyAfter,
                                  bool stop,
                                  bool damageNow,
                                  int early,
                                  bool abortAtRest,
                                  Step&& step,
                                  Query&& query,
                                  Damage&& damage,
                                  Rest&& rest,
                                  Distance&& distance,
                                  Defer&& defer) {
        horizon = std::clamp(horizon, 0, 64);
        readyAfter = (std::max)(0, readyAfter);
        if (readyAfter > horizon)
            return {};
        std::array<State, 65> coast{};
        coast[0] = initial;
        if (!stop) {
            auto state = initial;
            for (int time = 0; time <= horizon; ++time) {
                if (time >= readyAfter)
                    if (auto hit = query(state, time))
                        return {-1, time, 0.f, state, hit};
                if (time == horizon || !step(state, time, false, -1))
                    break;
            }
            return {};
        }
        // Already exposed and ready: delaying maximum braking cannot restore the
        // opening that has already occurred. Keep braking even if future HC is
        // unattainable; actual firing still performs its independent HC check.
        const bool immediate = damageNow && readyAfter == 0;
        std::array<ShotPlan<State, Hit>, 65> plans{};
        int earliest = horizon + 1, latest = -1;
        float leastOvershoot = 0.f;
        for (int start = 0; start <= horizon && start <= earliest; ++start) {
            // Test braking now before even constructing later coasting paths.
            // Extend the shared prefix only when a later start is still needed.
            if (start > 0) {
                if (immediate)
                    break;
                auto next = coast[start - 1];
                if (!step(next, start - 1, false, -1))
                    break;
                coast[start] = next;
            }
            auto state = coast[start], opening = initial;
            bool hadDamage = damageNow;

            struct Deferred {
                State state;
                int time;
            };

            std::array<Deferred, 65> skipped{};
            int skippedCount = 0;
            for (int time = start; time <= horizon && time <= earliest; ++time) {
                if (!hadDamage) {
                    if (defer(state, time))
                        skipped[skippedCount++] = {state, time};
                    else if (damage(state, time)) {
                        hadDamage = true;
                        opening = state;
                    }
                }
                if (abortAtRest && !hadDamage && rest(state, time))
                    break;
                if (time >= readyAfter)
                    if (auto hit = query(state, time)) {
                        if (!damageNow) {
                            // Locate the opening only after both MD and HC accept
                            // the endpoint. Coasting prefixes shared by many failed
                            // brake schedules need no penetration/HC scans at all.
                            bool found = false;
                            for (int i = 1; i <= start; ++i)
                                if (damage(coast[i], i)) {
                                    opening = coast[i];
                                    found = true;
                                    break;
                                }
                            if (!found)
                                for (int i = 0; i < skippedCount; ++i)
                                    if (damage(skipped[i].state, skipped[i].time)) {
                                        opening = skipped[i].state;
                                        break;
                                    }
                        }
                        const float travel = hadDamage ? distance(opening, state) : 0.f;
                        plans[start] = {start, time, travel, state, hit};
                        if (time < earliest || (time == earliest && (latest < 0 || travel < leastOvershoot - .001f))) {
                            earliest = time;
                            latest = start;
                            leastOvershoot = travel;
                        } else if (time == earliest && travel <= leastOvershoot + .001f)
                            latest = start;
                        break;
                    }
                if (time == horizon || time == earliest || !step(state, time, true, start))
                    break;
            }
        }
        if (latest < 0) {
            ShotPlan<State, Hit> none;
            if (immediate)
                none.brakeStart = 0;
            return none;
        }
        // Extra lead must not move the feasible shooting window later or stop
        // behind cover. It chooses only earlier starts reaching the same window.
        for (int start = (std::max)(0, latest - std::clamp(early, 0, 16)); start <= latest; ++start)
            if (plans[start].fireTick == earliest && plans[start].overshoot <= leastOvershoot + .001f)
                return plans[start];
        return plans[latest];
    }
} // namespace Rage
