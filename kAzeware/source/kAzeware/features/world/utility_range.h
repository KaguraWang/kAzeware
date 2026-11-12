#pragma once
class ViewMatrix;

namespace World::UtilityRange {
    // Network-update thread builds the terrain mesh; Present only projects it.
    void Update();
    void Draw(const ViewMatrix& view);
} // namespace World::UtilityRange
