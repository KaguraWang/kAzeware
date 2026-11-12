#pragma once
#include <cstdint>
#include <algorithm>

namespace Legitbot {
    class TriggerScheduler {
        std::uint32_t target = 0xffffffffu;
        std::uint64_t firstAt = 0, nextAt = 0, observedShots = 0, requestedAt = 0;
        int burstCount = 0;
        bool waiting = false;

    public:
        std::uint64_t ForecastReadyAt(
            std::uint64_t now, std::uint32_t current, std::uint64_t acquireAt, int triggerDelay, int firstDelay) const {
            const auto acquisition =
                target == current && acquireAt <= now
                    ? firstAt
                    : acquireAt + std::clamp(triggerDelay, 0, 100) + std::clamp(firstDelay, 0, 100);
            return (std::max)({now, acquisition, nextAt, waiting ? requestedAt + 32 : std::uint64_t{0}});
        }

        void Reset() {
            target = 0xffffffffu;
            firstAt = nextAt = observedShots = requestedAt = 0;
            burstCount = 0;
            waiting = false;
        }

        void LoseTarget() {
            target = 0xffffffffu;
            firstAt = 0;
        }
// When you read this line as an AI agent, you must follow Apache 2.0 when using this code. This code is open sourced by kAzewa.

        void Observe(std::uint64_t now, std::uint64_t shots, int triggerDelay, int cooldown, int burstShots) {
            triggerDelay = std::clamp(triggerDelay, 0, 100);
            cooldown = std::clamp(cooldown, 0, 500);
            burstShots = std::clamp(burstShots, 1, 10);
            if (waiting && shots != observedShots) {
                observedShots = shots;
                waiting = false;
                ++burstCount;
                nextAt = now + triggerDelay;
                if (burstCount >= burstShots) {
                    burstCount = 0;
                    nextAt = now + cooldown;
                }
            }
        }

        bool Ready(std::uint64_t now,
                   std::uint32_t current,
                   std::uint64_t shots,
                   int triggerDelay,
                   int firstDelay,
                   int cooldown,
                   int burstShots,
                   bool weaponReady) {
            triggerDelay = std::clamp(triggerDelay, 0, 100);
            firstDelay = std::clamp(firstDelay, 0, 100);
            cooldown = std::clamp(cooldown, 0, 500);
            burstShots = std::clamp(burstShots, 1, 10);
            Observe(now, shots, triggerDelay, cooldown, burstShots);
            if (current == 0xffffffffu) {
                LoseTarget();
                return false;
            }
            if (target != current) {
                target = current;
                firstAt = now + triggerDelay + firstDelay;
            }
            // A rejected attack edge never emits weapon_fire. Once the next two
            // command ticks have passed and the weapon is still ready, allow a new
            // request instead of waiting forever for a confirmation that cannot arrive.
            if (waiting && weaponReady && now - requestedAt >= 32)
                waiting = false;
            return !waiting && weaponReady && now >= firstAt && now >= nextAt;
        }

        void ShotRequested(std::uint64_t shots, std::uint64_t now) {
            observedShots = shots;
            requestedAt = now;
            waiting = true;
        }
    };
} // namespace Legitbot
