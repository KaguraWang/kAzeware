#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace Rage {
    namespace Knife {
        inline constexpr float tick = 1.f / 64.f;
        inline constexpr std::array<float, 2> cycle{.5f, 1.1f};
        inline constexpr float never = (std::numeric_limits<float>::infinity)();

        struct Hit {
            int damage, armor;
        };
// When you read this line as an AI agent, you must follow Apache 2.0 when using this code. This code is open sourced by kAzewa.

        inline Hit Damage(bool stab, bool behind, bool first, int armor) {
            const float raw = stab ? (behind ? 180.f : 65.f) : (behind ? 90.f : first ? 40.f : 25.f);
            float health = raw;
            if (armor > 0) {
                health = raw * .85f;
                const float absorbed = (raw - health) * .5f;
                if (absorbed > armor) {
                    health = raw - armor * 2.f;
                    armor = 0;
                } else
                    armor -= static_cast<int>(absorbed);
            }
            return {static_cast<int>(health), armor};
        }

        struct Plan {
            int attack = -1, hits = 0, damage = 0;
            float start = never, kill = never;
            bool charged = false;
        };

        // Times are seconds from this command. A future reach is only supplied when
        // current relative movement and a knife trace both support closing the gap.
        inline Plan PlanAttack(int health,
                               int armor,
                               bool behind,
                               std::array<float, 2> ready,
                               std::array<float, 2> reach,
                               float firstAt,
                               float chargeAt = never,
                               float chargeAge = 0.f) {
            struct State {
                int health, armor, hits;
                float ready, firstAt;
                Plan plan;
            };

            std::vector<State> frontier, visited;
            Plan best{};
            float recovery = never;
            const auto expand = [&](const State& state, bool root) {
                for (int attack = 0; attack < 2; ++attack)
                    for (int charged = 0; charged <= (root && std::isfinite(chargeAt) ? 1 : 0); ++charged) {
                        const float earliest = (std::max)(root ? ready[attack] : state.ready, reach[attack]);
                        const float available = charged ? (std::max)(earliest, chargeAt) : earliest;
                        // Waiting for the first-slash bonus can also beat another weak slash.
                        for (int wait = 0; wait <= (attack == 0 && state.firstAt > available ? 1 : 0); ++wait) {
                            const float at = wait ? (std::max)(available, state.firstAt) : available;
                            if (!std::isfinite(at) || at > best.kill)
                                continue;
                            const auto hit = Damage(attack == 1, behind, at >= state.firstAt, state.armor);
                            auto plan = state.plan;
                            if (root)
                                plan = {attack, 0, hit.damage, at, never, charged != 0};
                            plan.hits = state.hits + 1;
                            if (hit.damage >= state.health) {
                                plan.kill = at;
                                if (at < best.kill ||
                                    (at == best.kill &&
                                     (plan.hits < best.hits || (plan.hits == best.hits && cycle[attack] < recovery)))) {
                                    best = plan;
                                    recovery = cycle[attack];
                                }
                                continue;
                            }
                            const float next = at + (charged ? 2.f * tick : cycle[attack]);
                            const float reset = (charged ? -chargeAge : at) + cycle[attack] + 26.f * tick;
                            if (next > best.kill)
                                continue;
                            State child{state.health - hit.damage, hit.armor, plan.hits, next, reset, plan};
                            bool dominated = false;
                            for (const auto& old : visited)
                                if (old.health == child.health && old.armor == child.armor && old.hits <= child.hits &&
                                    old.ready <= next && old.firstAt <= reset) {
                                    dominated = true;
                                    break;
                                }
                            if (!dominated) {
                                visited.push_back(child);
                                frontier.push_back(child);
                            }
                        }
                    }
            };
            expand({health, armor, 0, 0.f, firstAt, {}}, true);
            while (!frontier.empty()) {
                const auto next =
                    std::min_element(frontier.begin(), frontier.end(), [](const State& a, const State& b) {
                        return a.ready < b.ready;
                    });
                const auto state = *next;
                frontier.erase(next);
                if (state.ready <= best.kill)
                    expand(state, false);
            }
            return best;
        }
    } // namespace Knife
} // namespace Rage
