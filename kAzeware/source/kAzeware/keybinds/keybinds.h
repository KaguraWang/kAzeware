#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <variant>
#include <type_traits>
#include <vector>
#include "../../../external/imgui/imgui.h"
#include "../../../external/json/json.hpp"

enum class KeyMode : int { Always = 0, Hold = 1, Toggle = 2 };
using BindValue = std::variant<bool, int, float, ImVec4>;
enum class BindKind { Boolean, Integer, Float, Color };

struct KeyBinding {
    std::uint64_t id = 0;
    int key = 0;
    KeyMode mode = KeyMode::Hold;
    BindValue value = false;
    bool active = false;
    std::uint64_t order = 0;
};

struct BindingTarget {
    std::string id, label;
    BindKind kind = BindKind::Boolean;
    std::function<void*()> resolve;
    std::function<void()> changed;
    std::function<BindValue()> read;
    std::function<void(const BindValue&)> write;
    BindValue base = false, applied = false;
    std::function<bool()> eligible;
    float displayScale = 1.f, displayOffset = 0.f;
    bool overriding = false;
    bool bounded = false;
    float minimum = 0.f, maximum = 0.f;
    std::vector<KeyBinding> bindings;
};

struct KeybindSnapshot {
    const char* name = "";
    int key = 0, mode = 0;
    bool active = false, enabled = false;
    float value = -1.f;
};

class Keybinds {
public:
    void initialize();
    void registerTarget(const std::string& id,
                        BindKind kind,
                        std::function<void*()> resolve,
                        const std::string& label = {},
                        std::function<void()> changed = {});

    template <class T>
    void
    registerValue(const std::string& id, T& value, const std::string& label = {}, std::function<void()> changed = {}) {
        BindKind kind;
        if constexpr (std::is_same_v<T, bool>)
            kind = BindKind::Boolean;
        else if constexpr (std::is_same_v<T, int>)
            kind = BindKind::Integer;
        else if constexpr (std::is_same_v<T, float>)
            kind = BindKind::Float;
        else
            kind = BindKind::Color;
        registerTarget(
            id,
            kind,
            [p = &value]() -> void* {
                return p;
            },
            label,
            std::move(changed));
    }

    BindingTarget* find(void* value);
    BindingTarget* find(const std::string& id);
    void contextMenu(void* value, const char* label, bool bounded = false, float low = 0.f, float high = 0.f);
    void contextMenu(const std::string& id, const char* label);
    void manualChange(void* value);
    void manualChange(const std::string& id, const BindValue& value);

    template <class T> T baseValue(const T& value) const {
        for (const auto& [id, target] : targets_)
            if (target.resolve() == &value && target.overriding)
                return std::get<T>(target.base);
        return value;
    }

    void onWindowMessage(unsigned msg, std::uintptr_t wParam, std::intptr_t lParam);
    void pollInputs(bool menuOpen);
    void processKeyEvent(int key, bool down, bool allowActivation);

    bool isActive(bool& feature) const {
        return feature;
    }

    bool isListening() const {
        return listening_ != 0;
    }

    int getKey(bool& feature) const;
    int getMode(bool& feature) const;
    void setKey(bool& feature, int key);
    void setMode(bool& feature, int mode);
    void resetToDefaults();
    void clearAllToggles();
    nlohmann::json save();
    void load(const nlohmann::json& json);
    int listSnapshots(KeybindSnapshot* out, int maxOut) const;
    static void formatKeyName(int key, char* out, size_t size);

    KeyBinding& addBinding(const std::string& target, int key, KeyMode mode, BindValue value = false);
    void removeBinding(const std::string& target, std::uint64_t binding);

private:
    struct KeyEvent {
        int key;
        bool down;
        bool raw = false;
    };

    std::map<std::string, BindingTarget> targets_;
    std::mutex eventsMutex_;
    std::vector<KeyEvent> events_;
    std::array<bool, 256> rawKeys_{}; // Protected by eventsMutex_; raw input owns these keys.
    std::array<bool, 256> down_{};
    bool initialized_ = false, sampled_ = false, waitRelease_ = false;
    std::uint64_t nextId_ = 1, nextOrder_ = 1, listening_ = 0;
    void apply(BindingTarget& target);
    void reconcile();
    void editor(BindingTarget& target);
    void loadTarget(BindingTarget& target, const nlohmann::json& entry);
    static bool blocked(int key);
};

extern Keybinds keybind;
