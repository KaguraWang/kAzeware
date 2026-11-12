#pragma once
#include <cstdint>
struct Vector_t;

namespace World {
    namespace Particles {
        // Game-thread marker using the same native manager as world effects.
        void
        PeekMarker(const Vector_t* origin, const Vector_t& color, float alpha, float radius, bool filled, float glow);
    } // namespace Particles

    namespace Weather {
        // Resolve GameParticleManager + ResourceSystem (Hooks::init).
        void Install();
        // Present/FSN: spawn+follow engine particles (snow/ash/rain).
        void Update();
        // No-op - engine particles only (no ImGui overlay).
        void Draw();
        void Shutdown();
        void ApplyMapInfo(uintptr_t mapInfo);
        // LevelInit: extract embedded .vpcf_c + re-arm warm.
        void OnLevelChange();
        // round_start / new match - particles + map wetness die each round; force rebuild.
        void OnRoundStart();
        // FireEventClientSide: round_start / round_officially_started / etc.
        void OnGameEvent(void* gameEvent);
        // FrameStageNotify (non-render): spawn+destroy warm for custom particles.
        void WarmTick();
    } // namespace Weather
} // namespace World
