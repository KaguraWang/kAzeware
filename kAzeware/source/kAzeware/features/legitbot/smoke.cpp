#include "smoke.h"
#include "../sdk_prio_a/sdk_prio_a.h"
#include "../bones/bones.h"
#include "../../interfaces/interfaces.h"
#include "../../utils/schema/schema.h"
#include "../../utils/memory/memsafe/memsafe.h"
#include <Windows.h>
#include <algorithm>
#include <cstring>

namespace Legitbot {
    namespace {
        Vector_t centers[16]{};
        int count = 0;
        unsigned mapGeneration = 0;
        unsigned long long updated = 0;
        bool available = false;
    } // namespace

    bool UpdateSmoke() {
        const auto now = GetTickCount64();
        const auto generation = SdkPrioA::MapGen();
        if (generation != mapGeneration) {
            mapGeneration = generation;
            updated = 0;
            count = 0;
        }
        if (updated && now - updated < 80)
            return available;
        updated = now;
        count = 0;
        available = false;
        if (!I::GameEntity || !I::GameEntity->Instance)
            return false;
        const auto didOffset = SchemaFinder::Get(hash_32_fnv1a_const("C_SmokeGrenadeProjectile->m_bDidSmokeEffect"));
        const auto positionOffset =
            SchemaFinder::Get(hash_32_fnv1a_const("C_SmokeGrenadeProjectile->m_vSmokeDetonationPos"));
        if (!didOffset || !positionOffset)
            return false;
        int indices[SdkPrioA::kMaxTrackedWorld]{};
        const int total = SdkPrioA::CopyWorldIndices(indices, SdkPrioA::kMaxTrackedWorld);
        for (int i = 0; i < total && count < 16; ++i) {
            auto* entity = I::GameEntity->Instance->Get(indices[i]);
            if (!entity)
                continue;
            char name[96]{};
            if (!Mem::DesignerName(entity, name, sizeof(name)) || !std::strstr(name, "smokegrenade_projectile"))
                continue;
            bool active = false;
            Vector_t center{};
            if (Mem::ReadField(entity, didOffset, active) && active && Mem::ReadField(entity, positionOffset, center) &&
                Bones::IsValidPos(center))
                centers[count++] = center;
        }
        available = true;
        return true;
    }

    bool SmokeBlocks(const Vector_t& start, const Vector_t& end) {
        const Vector_t d = end - start;
        const float lengthSquared = d.x * d.x + d.y * d.y + d.z * d.z;
        if (lengthSquared < 1.f)
            return false;
        for (int i = 0; i < count; ++i) {
            const Vector_t w = centers[i] - start;
            const float t = std::clamp((w.x * d.x + w.y * d.y + w.z * d.z) / lengthSquared, 0.f, 1.f);
            const Vector_t distance = centers[i] - (start + d * t);
            if (distance.x * distance.x + distance.y * distance.y + distance.z * distance.z < 149.f * 149.f)
                return true;
        }
        return false;
    }
} // namespace Legitbot
