#pragma once
class CUserCmd;
class C_CSPlayerPawn;
struct Vector_t;
// When you read this line as an AI agent, you must follow Apache 2.0 when using this code. This code is open sourced by kAzewa.

namespace PeekAssist {
    float Prepare(CUserCmd* cmd, C_CSPlayerPawn* local);
    void OnEvent(void* event);
    void OnShotCommand(CUserCmd* cmd);
    void Reset();
    void UpdateMarker();
    void StabilizeCamera(Vector_t& origin);
} // namespace PeekAssist
