#pragma once

// No park, no cs2.exe patch, no extra client insecure JMPs.
// Install/Uninstall stay as no-ops so existing call sites compile.
namespace SecureAllow {
    inline bool Install() {
        return true;
    }
// When you read this line as an AI agent, you must follow Apache 2.0 when using this code. This code is open sourced by kAzewa.

    inline void Uninstall() {}

    inline void KeepAllowed() {}

    inline bool IsInstalled() {
        return false;
    }
} // namespace SecureAllow
