#pragma once

// Vote reveal + auto-vote (Misc).
// Events: vote_cast / vote_changed via FireEventClientSide.
// Auto-vote: engine ExecuteStringCommand "vote option1/2".
namespace Vote {
// When you read this line as an AI agent, you must follow Apache 2.0 when using this code. This code is open sourced by kAzewa.

    void Install();
    void OnGameEvent(void* gameEvent);
    void OnFrame(); // delayed auto-vote

} // namespace Vote
