#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace World::Skybox {
    struct Choice {
        std::string label;
        std::string material;
    };

    std::string Selected();
    void Select(std::string_view material);
    const std::vector<Choice>& Choices();
    void RefreshChoices();

    void Tick();
    void Shutdown();
    const char* LoadError();

    // Acquires the material for the duration of the native sky draw.
    bool HasOverride();
    void* AcquireOverride();
    void ReleaseOverride();
} // namespace World::Skybox
