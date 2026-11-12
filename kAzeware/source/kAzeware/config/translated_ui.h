#pragma once
#include "language.h"
#include "../../../external/imgui/imgui.h"
#include <utility>
#include <vector>

// Translation is applied at the rendering boundary; model/config identifiers stay English.
namespace UiText {
    inline std::string PlainFormat(const char* format) {
        std::string text = Lang::T(format);
        for (size_t at = 0; (at = text.find("%%", at)) != text.npos; at++)
            text.erase(at, 1);
        return text;
    }

    template <class... Args> bool Button(const char* label, Args&&... args) {
        const auto id = Lang::Label(label);
        return ImGui::Button(id.c_str(), std::forward<Args>(args)...);
    }

    template <class... Args> bool SmallButton(const char* label, Args&&... args) {
        const auto id = Lang::Label(label);
        return ImGui::SmallButton(id.c_str(), std::forward<Args>(args)...);
    }

    template <class... Args> bool Checkbox(const char* label, Args&&... args) {
        const auto id = Lang::Label(label);
        return ImGui::Checkbox(id.c_str(), std::forward<Args>(args)...);
    }

    template <class... Args> bool RadioButton(const char* label, Args&&... args) {
        const auto id = Lang::Label(label);
        return ImGui::RadioButton(id.c_str(), std::forward<Args>(args)...);
    }

    template <class... Args> bool Selectable(const char* label, Args&&... args) {
        const auto id = Lang::Label(label);
        return ImGui::Selectable(id.c_str(), std::forward<Args>(args)...);
    }

    template <class... Args> bool InputText(const char* label, Args&&... args) {
        const auto id = Lang::Label(label);
        return ImGui::InputText(id.c_str(), std::forward<Args>(args)...);
    }

    template <class... Args> bool ColorEdit4(const char* label, Args&&... args) {
        const auto id = Lang::Label(label);
        return ImGui::ColorEdit4(id.c_str(), std::forward<Args>(args)...);
    }

    template <class... Args> bool ColorPicker4(const char* label, Args&&... args) {
        const auto id = Lang::Label(label);
        return ImGui::ColorPicker4(id.c_str(), std::forward<Args>(args)...);
    }

    template <class... Args> bool BeginCombo(const char* label, Args&&... args) {
        const auto id = Lang::Label(label);
        return ImGui::BeginCombo(id.c_str(), std::forward<Args>(args)...);
    }

    template <class... Args> void Text(const char* format, Args&&... args) {
        if constexpr (sizeof...(args) == 0) {
            const auto text = PlainFormat(format);
            ImGui::Text("%s", text.c_str());
        } else
            ImGui::Text(Lang::F(format), std::forward<Args>(args)...);
    }

    template <class... Args> void TextDisabled(const char* format, Args&&... args) {
        if constexpr (sizeof...(args) == 0) {
            const auto text = PlainFormat(format);
            ImGui::TextDisabled("%s", text.c_str());
        } else
            ImGui::TextDisabled(Lang::F(format), std::forward<Args>(args)...);
    }

    template <class... Args> void TextWrapped(const char* format, Args&&... args) {
        if constexpr (sizeof...(args) == 0) {
            const auto text = PlainFormat(format);
            ImGui::TextWrapped("%s", text.c_str());
        } else
            ImGui::TextWrapped(Lang::F(format), std::forward<Args>(args)...);
    }

    template <class... Args> void SetTooltip(const char* format, Args&&... args) {
        if constexpr (sizeof...(args) == 0) {
            const auto text = PlainFormat(format);
            ImGui::SetTooltip("%s", text.c_str());
        } else
            ImGui::SetTooltip(Lang::F(format), std::forward<Args>(args)...);
    }

    template <class... Args> void TextColored(const ImVec4& color, const char* format, Args&&... args) {
        if constexpr (sizeof...(args) == 0) {
            const auto text = PlainFormat(format);
            ImGui::TextColored(color, "%s", text.c_str());
        } else
            ImGui::TextColored(color, Lang::F(format), std::forward<Args>(args)...);
    }

    inline void TextUnformatted(const char* text) {
        ImGui::TextUnformatted(Lang::T(text));
    }

    template <class... Args> bool InputTextWithHint(const char* label, const char* hint, Args&&... args) {
        const auto id = Lang::Label(label);
        return ImGui::InputTextWithHint(id.c_str(), Lang::T(hint), std::forward<Args>(args)...);
    }

    inline bool Combo(const char* label, int* current, const char* const items[], int count, int height = -1) {
        std::vector<const char*> translated;
        translated.reserve(count);
        for (int i = 0; i < count; i++)
            translated.push_back(Lang::T(items[i]));
        const auto id = Lang::Label(label);
        return ImGui::Combo(id.c_str(), current, translated.data(), count, height);
    }

    inline bool SliderFloat(const char* label,
                            float* value,
                            float low,
                            float high,
                            const char* format = "%.3f",
                            ImGuiSliderFlags flags = 0) {
        const auto id = Lang::Label(label);
        return ImGui::SliderFloat(id.c_str(), value, low, high, Lang::F(format), flags);
    }

    inline bool
    SliderInt(const char* label, int* value, int low, int high, const char* format = "%d", ImGuiSliderFlags flags = 0) {
        const auto id = Lang::Label(label);
        return ImGui::SliderInt(id.c_str(), value, low, high, Lang::F(format), flags);
    }

    inline bool InputFloat(const char* label,
                           float* value,
                           float step = 0,
                           float fast = 0,
                           const char* format = "%.3f",
                           ImGuiInputTextFlags flags = 0) {
        const auto id = Lang::Label(label);
        return ImGui::InputFloat(id.c_str(), value, step, fast, Lang::F(format), flags);
    }

    inline bool InputInt(const char* label, int* value, int step = 1, int fast = 100, ImGuiInputTextFlags flags = 0) {
        const auto id = Lang::Label(label);
        return ImGui::InputInt(id.c_str(), value, step, fast, flags);
    }
} // namespace UiText
