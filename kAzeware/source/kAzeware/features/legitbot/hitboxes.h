#pragma once
#include "../bones/bones.h"
#include "../../../cs2/entity/handle.h"

namespace Legitbot {
    void BeginHitboxFrame();
    int ReadHitboxes(C_CSPlayerPawn* pawn, CBaseHandle handle, unsigned mask, Bones::GroupCapsule* out, int maxOut);
    bool Visible(const Vector_t& start, const Vector_t& end, C_CSPlayerPawn* local, C_CSPlayerPawn* target);
} // namespace Legitbot
