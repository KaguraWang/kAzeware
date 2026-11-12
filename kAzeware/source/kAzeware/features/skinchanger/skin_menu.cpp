#include "../../config/translated_ui.h"
#include "skin_menu.h"
#include "skin_items.h"
#include "skin_preview.h"
#include "skinchanger.h"
#include "../custom_paint/custom_paint.h"

#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>

#include "../../../../external/imgui/imgui.h"
#include "../../config/config.h"
#include "../../menu/menu_ui.h"

namespace {
    enum SubTab { Weapons = 0, Knives, Gloves, Agents, SubTabCount };

    enum Page { Page_Grid = 0, Page_Browser, Page_CustomAgents };

    SubTab g_sub = Weapons;
    Page g_page = Page_Grid;
    int g_skinTeam = 2;
    uint16_t g_browsingDef = 0;
    int g_browsingAgentTeam = 0; // 2=T, 3=CT
    std::vector<std::string> g_customAgentModels;
    char g_customAgentFolder[512]{};
    bool g_customFolderError = false;

    char g_search[64]{};
    float g_wear = 0.0001f;
    int g_seed = 0;
    bool g_st = false;
    int g_stCount = 1337;
    char g_tag[64]{};
    int g_editingPaint = 0;
    bool g_resetSkin = false;
    ImVec2 g_editorPosition{};

    ImU32 GetRarityColor(int rarity) {
        switch (rarity) {
        case 1:
            return IM_COL32(176, 195, 217, 255); // Consumer
        case 2:
            return IM_COL32(94, 152, 217, 255); // Industrial
        case 3:
            return IM_COL32(75, 105, 255, 255); // Mil-Spec
        case 4:
            return IM_COL32(136, 71, 255, 255); // Restricted
        case 5:
            return IM_COL32(211, 44, 230, 255); // Classified
        case 6:
            return IM_COL32(235, 75, 75, 255); // Covert
        case 7:
            return IM_COL32(255, 215, 0, 255); // Gold / Extraordinary
        default:
            return IM_COL32(140, 145, 155, 255);
        }
    }

    const char* GetRarityName(int rarity) {
        switch (rarity) {
        case 1:
            return "Consumer Grade";
        case 2:
            return "Industrial Grade";
        case 3:
            return "Mil-Spec Grade";
        case 4:
            return "Restricted";
        case 5:
            return "Classified";
        case 6:
            return "Covert";
        case 7:
            return "Extraordinary";
        default:
            return "Standard";
        }
    }

    const char* GetWearTier(float wear, ImVec4& outCol) {
        if (wear <= 0.07f) {
            outCol = ImVec4(0.35f, 0.95f, 0.45f, 1.f);
            return "FN";
        }
        if (wear <= 0.15f) {
            outCol = ImVec4(0.3f, 0.85f, 0.85f, 1.f);
            return "MW";
        }
        if (wear <= 0.38f) {
            outCol = ImVec4(0.95f, 0.85f, 0.25f, 1.f);
            return "FT";
        }
        if (wear <= 0.45f) {
            outCol = ImVec4(0.95f, 0.55f, 0.2f, 1.f);
            return "WW";
        }
        outCol = ImVec4(0.95f, 0.3f, 0.3f, 1.f);
        return "BS";
    }

    bool NameMatch(const char* name, const char* filter) {
        if (!filter || !filter[0])
            return true;
        if (!name)
            return false;
        const char* n = name;
        const char* f = filter;
        while (*n) {
            const char* a = n;
            const char* b = f;
            while (*a && *b) {
                const char ca = (*a >= 'A' && *a <= 'Z') ? char(*a + 32) : *a;
                const char cb = (*b >= 'A' && *b <= 'Z') ? char(*b + 32) : *b;
                if (ca != cb)
                    break;
                ++a;
                ++b;
            }
            if (!*b)
                return true;
            ++n;
        }
        return false;
    }

    int AppliedPaint(SubTab sub, int def) {
        if (sub == Knives)
            return (Config::SkinTeam(g_skinTeam).skin_knife && Config::SkinTeam(g_skinTeam).skin_knife_def == def)
                       ? Config::SkinTeam(g_skinTeam).skin_knife_paint
                       : 0;
        if (sub == Gloves)
            return (Config::SkinTeam(g_skinTeam).skin_glove && Config::SkinTeam(g_skinTeam).skin_glove_def == def)
                       ? Config::SkinTeam(g_skinTeam).skin_glove_paint
                       : 0;
        if (sub == Weapons) {
            Config::WeaponSkin ws{};
            if (Config::SkinWeapon_Find(g_skinTeam, def, ws))
                return ws.paint;
            return 0;
        }
        return 0;
    }

    void SyncActiveProperties(SubTab sub, int def) {
        g_wear = 0.0001f;
        g_seed = 0;
        g_st = false;
        g_stCount = 1337;
        g_tag[0] = 0;
        if (sub == Knives && Config::SkinTeam(g_skinTeam).skin_knife &&
            Config::SkinTeam(g_skinTeam).skin_knife_def == def) {
            g_wear = Config::SkinTeam(g_skinTeam).skin_knife_wear;
            g_seed = Config::SkinTeam(g_skinTeam).skin_knife_seed;
            g_st = Config::SkinTeam(g_skinTeam).skin_knife_stattrak;
            g_stCount = Config::SkinTeam(g_skinTeam).skin_knife_stattrak_count;
            std::snprintf(g_tag, sizeof(g_tag), "%s", Config::SkinTeam(g_skinTeam).skin_knife_tag);
        } else if (sub == Gloves && Config::SkinTeam(g_skinTeam).skin_glove &&
                   Config::SkinTeam(g_skinTeam).skin_glove_def == def) {
            g_wear = Config::SkinTeam(g_skinTeam).skin_glove_wear;
            g_seed = Config::SkinTeam(g_skinTeam).skin_glove_seed;
            g_st = Config::SkinTeam(g_skinTeam).skin_glove_stattrak;
            g_stCount = Config::SkinTeam(g_skinTeam).skin_glove_stattrak_count;
            std::snprintf(g_tag, sizeof(g_tag), "%s", Config::SkinTeam(g_skinTeam).skin_glove_tag);
        } else if (sub == Weapons) {
            Config::WeaponSkin ws{};
            if (Config::SkinWeapon_Find(g_skinTeam, def, ws)) {
                g_wear = ws.wear;
                g_seed = ws.seed;
                g_st = ws.stattrak;
                g_stCount = ws.stattrak_count;
                std::snprintf(g_tag, sizeof(g_tag), "%s", ws.tag);
            } else {
                g_wear = 0.0001f;
                g_seed = 0;
                g_st = false;
                g_tag[0] = 0;
            }
        }
    }

    void ApplySkin(SubTab sub, int def, int paintId, int team) {
        if (sub == Knives) {
            Config::SkinTeam(team).skin_knife = true;
            Config::SkinTeam(team).skin_knife_def = def;
            Config::SkinTeam(team).skin_knife_paint = paintId;
            Config::SkinTeam(team).skin_knife_wear = g_wear;
            Config::SkinTeam(team).skin_knife_seed = g_seed;
            Config::SkinTeam(team).skin_knife_stattrak = g_st;
            Config::SkinTeam(team).skin_knife_stattrak_count = g_stCount;
            std::snprintf(
                Config::SkinTeam(team).skin_knife_tag, sizeof(Config::SkinTeam(team).skin_knife_tag), "%s", g_tag);
        } else if (sub == Gloves) {
            Config::SkinTeam(team).skin_glove = true;
            Config::SkinTeam(team).skin_glove_def = def;
            Config::SkinTeam(team).skin_glove_paint = paintId;
            Config::SkinTeam(team).skin_glove_wear = g_wear;
            Config::SkinTeam(team).skin_glove_seed = g_seed;
            Config::SkinTeam(team).skin_glove_stattrak = g_st;
            Config::SkinTeam(team).skin_glove_stattrak_count = g_stCount;
            std::snprintf(
                Config::SkinTeam(team).skin_glove_tag, sizeof(Config::SkinTeam(team).skin_glove_tag), "%s", g_tag);
        } else if (sub == Weapons) {
            Config::WeaponSkin ws{};
            ws.paint = paintId;
            ws.wear = g_wear;
            ws.seed = g_seed;
            ws.stattrak = g_st;
            ws.stattrak_count = g_stCount;
            std::snprintf(ws.tag, sizeof(ws.tag), "%s", g_tag);
            Config::SkinWeapon_Set(team, def, ws);
        }
        // Knife/weapon picks self-apply via per-weapon signature diff next FSN -
        // only the HUD icon burst is needed. Gloves/agents need their own
        // pipeline kick, so they keep the full RefreshAll.
        if (sub == Knives || sub == Weapons)
            SkinChanger::NotifySkinsChanged();
        else
            SkinChanger::RefreshAll();
    }

    void RemoveSkin(SubTab sub, int def, int team) {
        if (sub == Knives) {
            Config::SkinTeam(team).skin_knife = false;
            Config::SkinTeam(team).skin_knife_def = 0;
            Config::SkinTeam(team).skin_knife_paint = 0;
            Config::SkinTeam(team).skin_knife_wear = 0.f;
            Config::SkinTeam(team).skin_knife_seed = 0;
            Config::SkinTeam(team).skin_knife_stattrak = false;
            Config::SkinTeam(team).skin_knife_tag[0] = '\0';
        } else if (sub == Gloves) {
            Config::SkinTeam(team).skin_glove = false;
            Config::SkinTeam(team).skin_glove_def = 0;
            Config::SkinTeam(team).skin_glove_paint = 0;
            Config::SkinTeam(team).skin_glove_wear = 0.f;
            Config::SkinTeam(team).skin_glove_seed = 0;
            Config::SkinTeam(team).skin_glove_stattrak = false;
            Config::SkinTeam(team).skin_glove_tag[0] = '\0';
        } else if (sub == Weapons) {
            Config::SkinWeapon_Erase(team, def);
        }
        // IDA: WalkWeapons revert needs force + HUD burst; light Notify left stale HUD and crash on stale composite
        SkinChanger::RefreshAll();
    }

    const char* SkinToken(SkinItems::Item* item, int paintId) {
        if (!item || paintId <= 0)
            return nullptr;
        for (const auto& s : item->skins)
            if (s.id == paintId && !s.token.empty())
                return s.token.c_str();
        return nullptr;
    }

    const char* SkinDisplayName(SkinItems::Item* item, int paintId) {
        if (!item || paintId <= 0)
            return nullptr;
        for (const auto& s : item->skins)
            if (s.id == paintId && !s.name.empty())
                return s.name.c_str();
        return SkinToken(item, paintId);
    }

    void DrawTextEllipsis(const char* text, const ImVec2& minPos, float width, ImU32 color, bool center) {
        if (!text || !text[0] || width <= 2.f)
            return;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        text = Lang::T(text);
        const ImVec2 tsz = ImGui::CalcTextSize(text);
        const float lineH = tsz.y + 2.f;
        dl->PushClipRect(minPos, ImVec2(minPos.x + width, minPos.y + lineH), true);
        if (tsz.x <= width) {
            float x = minPos.x;
            if (center)
                x += (width - tsz.x) * 0.5f;
            dl->AddText(ImVec2(x, minPos.y), color, text);
            dl->PopClipRect();
            return;
        }
        const float ellW = ImGui::CalcTextSize("...").x;
        const float budget = width - ellW;
        int cut = 0;
        if (budget > 4.f) {
            int lo = 0, hi = (int)std::strlen(text);
            while (lo < hi) {
                const int mid = (lo + hi + 1) / 2;
                if (ImGui::CalcTextSize(text, text + mid).x <= budget)
                    lo = mid;
                else
                    hi = mid - 1;
            }
            cut = lo;
        }
        char buf[160];
        if (cut > 0 && cut < (int)sizeof(buf) - 4) {
            std::memcpy(buf, text, (size_t)cut);
            buf[cut] = '.';
            buf[cut + 1] = '.';
            buf[cut + 2] = '.';
            buf[cut + 3] = 0;
            dl->AddText(minPos, color, buf);
        } else {
            dl->AddText(minPos, color, "...");
        }
        dl->PopClipRect();
    }

    void DrawTextCentered(const char* text, const ImVec2& minPos, float width, ImU32 color) {
        DrawTextEllipsis(text, minPos, width, color, true);
    }

    void DrawBadgeCheck(ImDrawList* dl, const ImVec2& topPos, float size = 16.f) {
        const ImVec2 center(topPos.x + size * 0.5f, topPos.y + size * 0.5f);
        dl->AddCircleFilled(center, size * 0.5f, IM_COL32(40, 200, 90, 255));
        dl->AddLine(ImVec2(center.x - size * 0.26f, center.y),
                    ImVec2(center.x - size * 0.06f, center.y + size * 0.22f),
                    IM_COL32(15, 25, 20, 255),
                    1.8f * MenuUI::Layout::DpiMul());
        dl->AddLine(ImVec2(center.x - size * 0.06f, center.y + size * 0.22f),
                    ImVec2(center.x + size * 0.28f, center.y - size * 0.22f),
                    IM_COL32(15, 25, 20, 255),
                    1.8f * MenuUI::Layout::DpiMul());
    }

    struct GridLayout {
        float gap = 8.f;
        float cardW = 1.f;
        float cardH = 1.f;
        float imageBottom = 1.f;
        int columns = 1;
    };

    GridLayout MakeGridLayout(float availW, float minCardW = 118.f) {
        const float dpi = MenuUI::Layout::DpiMul();
        GridLayout out{};
        out.gap = 10.f;
        availW /= dpi;
        availW = (std::max)(1.f, availW);
        minCardW = (std::max)(96.f, minCardW);
        out.columns = (std::max)(1, static_cast<int>((availW + out.gap) / (minCardW + out.gap)));

        // Keep cards readable at wide resolutions instead of stretching one row
        // into oversized thumbnails. The final column absorbs the rounding remainder.
        while (out.columns < 12) {
            const float w = (availW - out.gap * (out.columns - 1)) / out.columns;
            if (w <= 170.f)
                break;
            ++out.columns;
        }

        out.cardW = (std::max)(1.f, (availW - out.gap * (out.columns - 1)) / out.columns);
        out.cardH = std::clamp(out.cardW * 0.90f, 100.f, 144.f);
        const float labelH = ImGui::GetFontSize() / dpi + 8.f;
        out.imageBottom = (std::max)(58.f, out.cardH - labelH - 8.f);
        out.gap *= dpi;
        out.cardW *= dpi;
        out.cardH *= dpi;
        out.imageBottom *= dpi;
        return out;
    }

    // Contain: whole model stays inside the well. scale < 1 insets so edges
    // never kiss the clip rect (cover-crop was chopping barrels / heads).
    void DrawImageContain(ImDrawList* dl,
                          ImTextureID tex,
                          const ImVec2& boxMin,
                          const ImVec2& boxMax,
                          float aspect,
                          float scale,
                          float rounding) {
        if (!tex || !dl)
            return;
        const float boxW = boxMax.x - boxMin.x;
        const float boxH = boxMax.y - boxMin.y;
        if (boxW <= 1.f || boxH <= 1.f)
            return;
        aspect = (aspect > 0.2f) ? aspect : 1.35f;
        scale = std::clamp(scale, 0.4f, 1.f);
        float iw = boxH * aspect;
        float ih = boxH;
        if (iw > boxW) {
            iw = boxW;
            ih = iw / aspect;
        }
        iw *= scale;
        ih *= scale;
        const float ix = boxMin.x + (boxW - iw) * 0.5f;
        const float iy = boxMin.y + (boxH - ih) * 0.5f;
        dl->PushClipRect(boxMin, boxMax, true);
        dl->AddImageRounded(ImTextureRef(tex),
                            ImVec2(ix, iy),
                            ImVec2(ix + iw, iy + ih),
                            ImVec2(0.f, 0.f),
                            ImVec2(1.f, 1.f),
                            IM_COL32_WHITE,
                            rounding);
        dl->PopClipRect();
    }

    void DrawCardChrome(
        ImDrawList* dl, const ImVec2& rmin, const ImVec2& rmax, bool equipped, bool hovered, float rounding) {
        if (equipped) {
            ImVec4 fill = MenuUI::Surface();
            fill.x = fill.x * 0.55f + MenuUI::Accent().x * 0.18f;
            fill.y = fill.y * 0.55f + MenuUI::Accent().y * 0.18f;
            fill.z = fill.z * 0.55f + MenuUI::Accent().z * 0.18f;
            fill.w = 0.92f;
            dl->AddRectFilled(rmin, rmax, MenuUI::ToU32(fill), rounding);
            dl->AddRect(rmin, rmax, MenuUI::AccentU32(0.95f), rounding, 0, 1.4f);
        } else if (hovered) {
            ImVec4 fill = MenuUI::Surface();
            fill.x += 0.05f;
            fill.y += 0.05f;
            fill.z += 0.05f;
            fill.w = 0.95f;
            dl->AddRectFilled(rmin, rmax, MenuUI::ToU32(fill), rounding);
            dl->AddRect(rmin, rmax, MenuUI::AccentU32(0.85f), rounding, 0, 1.2f);
        } else {
            dl->AddRectFilled(rmin, rmax, MenuUI::ToU32(MenuUI::Surface()), rounding);
            dl->AddRect(rmin, rmax, MenuUI::BorderU32(), rounding, 0, 1.0f);
        }
    }

    void DrawImageWell(
        ImDrawList* dl, const ImVec2& imgMin, const ImVec2& imgMax, ImTextureID tex, float aspect, float rounding) {
        if (!dl || imgMax.x <= imgMin.x || imgMax.y <= imgMin.y)
            return;
        dl->AddRectFilled(imgMin, imgMax, IM_COL32(8, 9, 12, 180), rounding);
        if (tex)
            DrawImageContain(
                dl,
                tex,
                ImVec2(imgMin.x + 2.f * MenuUI::Layout::DpiMul(), imgMin.y + 2.f * MenuUI::Layout::DpiMul()),
                ImVec2(imgMax.x - 2.f * MenuUI::Layout::DpiMul(), imgMax.y - 2.f * MenuUI::Layout::DpiMul()),
                aspect,
                0.78f,
                rounding);
    }
} // namespace

void SkinMenu::Draw() {
    const float dpi = MenuUI::Layout::DpiMul();
    SkinItems& mgr = GetSkinItems();
    if (!mgr.Ready()) {
        mgr.Scan();
        if (!mgr.Ready()) {
            UiText::TextDisabled("Loading game items schema...");
            return;
        }
    }
    auto& all = mgr.Items();
    SkinPreview& preview = GetSkinPreview();

    const char* subNames[SubTabCount] = {"Guns", "Knives", "Gloves", "Agents"};
    const SubTab oldSub = g_sub;
    int subIdx = static_cast<int>(g_sub);
    MenuUI::SubNav(subNames, SubTabCount, &subIdx);
    g_sub = static_cast<SubTab>(subIdx);
    if (g_sub != Agents) {
        ImGui::RadioButton("T##SkinTeam", &g_skinTeam, 2);
        ImGui::SameLine();
        ImGui::RadioButton("CT##SkinTeam", &g_skinTeam, 3);
    }

    if (g_sub != oldSub) {
        g_page = Page_Grid;
        g_browsingDef = 0;
        g_search[0] = 0;
    }

    // ------------------------------------------------------------------------
    // PAGE 1: GRID MODE
    // ------------------------------------------------------------------------
    if (g_page == Page_Grid) {
        SkinItems::Type itemType = SkinItems::None;
        switch (g_sub) {
        case Weapons:
            itemType = SkinItems::Weapon;
            break;
        case Knives:
            itemType = SkinItems::Knife;
            break;
        case Gloves:
            itemType = SkinItems::Glove;
            break;
        case Agents:
            itemType = SkinItems::Agent;
            break;
        default:
            break;
        }

        // Agent Team Selection (Clean Menu-Themed Cards with Fallback Default Image)
        if (g_sub == Agents) {
            const float availW = ImGui::GetContentRegionAvail().x;
            const float gap = 10.f * dpi;
            const float cardW = (availW - gap) * 0.5f;
            const float round = std::clamp(Config::menu_rounding, 2.f, 6.f) * dpi;

            auto DrawTeamCard =
                [&](const char* id, const char* title, const char* teamTag, ImVec4 tagCol, int teamId, int activeDef) {
                    MenuUI::BeginCard(id, cardW, true);
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    const ImVec2 rmin = ImGui::GetWindowPos();
                    const float innerW = ImGui::GetContentRegionAvail().x;

                    UiText::TextUnformatted(title);
                    ImGui::SameLine();
                    ImGui::PushStyleColor(ImGuiCol_Text, tagCol);
                    UiText::TextUnformatted(teamTag);
                    ImGui::PopStyleColor();

                    SkinItems::Item* eqAgent = nullptr;
                    if (activeDef > 0)
                        eqAgent = mgr.Find(static_cast<uint16_t>(activeDef));
                    if (!eqAgent) {
                        for (auto& it : all) {
                            if (it.type == SkinItems::Agent && it.team == teamId && !it.icon.empty()) {
                                eqAgent = &it;
                                break;
                            }
                        }
                    }

                    const auto custom = SkinChanger::CustomAgentModel(teamId);
                    ImTextureID atex = (ImTextureID)0;
                    if (custom.empty() && eqAgent && !eqAgent->icon.empty())
                        atex = preview.GetTexture(SkinPreview::AgentPath(eqAgent->icon.c_str()));

                    const float imgH = std::clamp(innerW * 0.48f, 96.f * dpi, 132.f * dpi);
                    const ImVec2 imgMin = ImGui::GetCursorScreenPos();
                    const ImVec2 imgMax(imgMin.x + innerW, imgMin.y + imgH);
                    DrawImageWell(dl, imgMin, imgMax, atex, 1.0f, round);
                    ImGui::Dummy(ImVec2(innerW, imgH + 6.f * dpi));

                    char labelBuf[128];
                    if (!custom.empty()) {
                        const auto name = custom.substr(custom.rfind('/') + 1);
                        snprintf(labelBuf, sizeof(labelBuf), "%s", name.substr(0, name.size() - 7).c_str());
                    } else if (activeDef > 0 && eqAgent)
                        snprintf(labelBuf, sizeof(labelBuf), "%s", eqAgent->name.c_str());
                    else
                        snprintf(labelBuf, sizeof(labelBuf), Lang::T("Default %s Agent"), (teamId == 2) ? "T" : "CT");

                    DrawTextEllipsis(labelBuf,
                                     ImGui::GetCursorScreenPos(),
                                     innerW,
                                     (activeDef > 0 || !custom.empty()) ? MenuUI::AccentU32()
                                                                        : MenuUI::ToU32(MenuUI::TextBright()),
                                     true);
                    ImGui::Dummy(ImVec2(0.f, ImGui::GetFontSize() + 8.f * dpi));

                    if (UiText::Button("Browse Officials", ImVec2(-1.f, 26.f * dpi))) {
                        g_browsingAgentTeam = teamId;
                        g_browsingDef = 0;
                        g_page = Page_Browser;
                        g_search[0] = 0;
                    }
                    if (UiText::Button("Or Custom", ImVec2(-1.f, 26.f * dpi))) {
                        g_browsingAgentTeam = teamId;
                        g_customAgentModels = SkinChanger::ScanCustomAgentModels();
                        g_page = Page_CustomAgents;
                        g_search[0] = 0;
                    }
                    if (activeDef > 0 || !custom.empty()) {
                        if (UiText::Button("Reset to Default", ImVec2(-1.f, 22.f * dpi))) {
                            if (teamId == 2)
                                Config::skin_agent_t = 0;
                            else
                                Config::skin_agent_ct = 0;
                            SkinChanger::SetCustomAgentModel(teamId, {});
                        }
                    }
                    (void)rmin;
                    MenuUI::EndCard();
                };

            DrawTeamCard("##agent_t", "Terrorist", "T", ImVec4(0.92f, 0.52f, 0.20f, 1.f), 2, Config::skin_agent_t);
            ImGui::SameLine(0.f, gap);
            DrawTeamCard(
                "##agent_ct", "Counter-Terrorist", "CT", ImVec4(0.32f, 0.62f, 0.95f, 1.f), 3, Config::skin_agent_ct);
            return;
        }

        // Search bar at the top of Grid
        ImGui::SetNextItemWidth(300.f * dpi);
        UiText::InputTextWithHint("##GridSearch", "Search weapons / knives / gloves...", g_search, sizeof(g_search));
        ImGui::Spacing();

        std::vector<SkinItems::Item*> filteredItems;
        for (auto& it : all) {
            if (it.type != itemType)
                continue;
            if (!NameMatch(it.name.c_str(), g_search))
                continue;
            filteredItems.push_back(&it);
        }

        ImGui::BeginChild("##MaycryGridChild", ImVec2(0.f, 0.f), ImGuiChildFlags_None);
        {
            const GridLayout grid = MakeGridLayout(ImGui::GetContentRegionAvail().x);
            const float spacing = grid.gap;
            const float cardW = grid.cardW;
            const float cardH = grid.cardH;
            const float round = std::clamp(Config::menu_rounding, 2.f, 6.f) * dpi;
            const int nCols = grid.columns;

            for (int i = 0; i < static_cast<int>(filteredItems.size()); ++i) {
                auto* item = filteredItems[i];
                if (!item)
                    continue;

                ImGui::PushID(item->def);
                if (i % nCols != 0)
                    ImGui::SameLine(0.f, spacing);

                const int appliedPaintId = AppliedPaint(g_sub, item->def);
                const bool isEquipped =
                    (appliedPaintId > 0 || (g_sub == Knives && Config::SkinTeam(g_skinTeam).skin_knife &&
                                            Config::SkinTeam(g_skinTeam).skin_knife_def == item->def));

                if (UiText::Selectable("##card_btn", false, 0, ImVec2(cardW, cardH))) {
                    g_browsingDef = item->def;
                    g_page = Page_Browser;
                    g_search[0] = 0;
                    SyncActiveProperties(g_sub, item->def);
                }

                const ImVec2 rmin = ImGui::GetItemRectMin();
                const ImVec2 rmax = ImGui::GetItemRectMax();
                const bool isHovered = ImGui::IsItemHovered();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                DrawCardChrome(dl, rmin, rmax, isEquipped, isHovered, round);

                if (!item->skinsReady && (isEquipped || item->type == SkinItems::Glove))
                    mgr.EnsureSkins(item->def);

                const char* kitTok = SkinToken(item, appliedPaintId);
                ImTextureID tex = (ImTextureID)0;
                if (kitTok && !item->simple.empty())
                    tex = preview.GetPaintTexture(item->simple.c_str(), kitTok);
                if (!tex && !item->simple.empty())
                    tex = preview.GetModelTexture(item->simple.c_str());
                if (!tex && item->type == SkinItems::Glove && !item->simple.empty() && !item->skins.empty())
                    tex = preview.GetPaintTexture(item->simple.c_str(), item->skins[0].token.c_str());

                const float imgH = grid.imageBottom;
                const ImVec2 imgMin(rmin.x + 8.f * dpi, rmin.y + 6.f * dpi);
                const ImVec2 imgMax(rmax.x - 8.f * dpi, rmin.y + imgH);
                DrawImageWell(
                    dl, imgMin, imgMax, tex, item->type == SkinItems::Glove ? 1.05f : 1.45f, round - 1.f * dpi);

                int rarity = 0;
                if (isEquipped && appliedPaintId > 0) {
                    for (const auto& s : item->skins) {
                        if (s.id == appliedPaintId) {
                            rarity = s.rarity;
                            break;
                        }
                    }
                }
                const ImU32 rCol = GetRarityColor(rarity);
                dl->AddRectFilled(ImVec2(rmin.x + 6.f * dpi, rmin.y + imgH + 2.f * dpi),
                                  ImVec2(rmax.x - 6.f * dpi, rmin.y + imgH + 4.f * dpi),
                                  rCol,
                                  1.f);

                if (isEquipped && appliedPaintId > 0) {
                    const char* sName = SkinDisplayName(item, appliedPaintId);
                    DrawTextCentered(sName ? sName : item->name.c_str(),
                                     ImVec2(rmin.x + 6.f * dpi, rmin.y + imgH + 7.f * dpi),
                                     cardW - 12.f * dpi,
                                     rCol);
                } else {
                    DrawTextCentered(item->name.c_str(),
                                     ImVec2(rmin.x + 6.f * dpi, rmin.y + imgH + 7.f * dpi),
                                     cardW - 12.f * dpi,
                                     MenuUI::ToU32(MenuUI::TextBright()));
                }

                if (isEquipped)
                    DrawBadgeCheck(dl, ImVec2(rmax.x - 18.f * dpi, rmin.y + 4.f * dpi), 14.f * dpi);

                ImGui::PopID();
            }
        }
        ImGui::EndChild();
    }
    // ------------------------------------------------------------------------
    // PAGE 2: BROWSER MODE
    // ------------------------------------------------------------------------
    else if (g_page == Page_CustomAgents) {
        const int team = g_browsingAgentTeam;
        if (UiText::Button("< Back to Teams", ImVec2(120.f * dpi, 28.f * dpi))) {
            g_page = Page_Grid;
            return;
        }
        ImGui::SameLine();
        if (UiText::Button("Refresh Models"))
            g_customAgentModels = SkinChanger::ScanCustomAgentModels();
        UiText::TextDisabled("%s", team == 2 ? "T" : "CT");
        const float addWidth = ImGui::CalcTextSize(Lang::T("Add Model Folder")).x +
                               ImGui::GetStyle().FramePadding.x * 2.f + ImGui::GetStyle().ItemSpacing.x;
        ImGui::SetNextItemWidth((std::max)(1.f, ImGui::GetContentRegionAvail().x - addWidth));
        UiText::InputTextWithHint(
            "##CustomAgentFolder", "Absolute model folder path...", g_customAgentFolder, sizeof(g_customAgentFolder));
        ImGui::SameLine();
        auto folders = SkinChanger::CustomAgentFolders();
        if (UiText::Button("Add Model Folder")) {
            const auto previousSize = folders.size();
            folders.emplace_back(g_customAgentFolder);
            SkinChanger::SetCustomAgentFolders(folders);
            folders = SkinChanger::CustomAgentFolders();
            g_customFolderError = folders.size() == previousSize;
            if (!g_customFolderError) {
                g_customAgentFolder[0] = 0;
                g_customAgentModels = SkinChanger::ScanCustomAgentModels();
            }
        }
        if (g_customFolderError)
            UiText::TextDisabled("Choose an existing folder that is not already listed.");
        for (size_t i = 0; i < folders.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            const bool remove = UiText::Button("Remove");
            ImGui::SameLine();
            ImGui::TextWrapped("%s", folders[i].c_str());
            ImGui::PopID();
            if (remove) {
                folders.erase(folders.begin() + i);
                SkinChanger::SetCustomAgentFolders(std::move(folders));
                g_customAgentModels = SkinChanger::ScanCustomAgentModels();
                break;
            }
        }
        UiText::TextWrapped(
            "Place compiled models in game/csgo/agents/models or game/csgo/characters/models. Preserve the original paths of their dependencies.");
        if (const char* error = SkinChanger::CustomAgentError(team))
            UiText::TextDisabled("%s", Lang::T(error));
        UiText::InputTextWithHint("##CustomAgentSearch", "Search model path...", g_search, sizeof(g_search));
        const auto selected = SkinChanger::CustomAgentModel(team);
        ImGui::BeginChild("##CustomAgentList");
        for (const auto& path : g_customAgentModels) {
            const char* label = path.c_str();
            if (!NameMatch(label, g_search))
                continue;
            if (ImGui::Selectable(label, path == selected)) {
                (team == 2 ? Config::skin_agent_t : Config::skin_agent_ct) = 0;
                Config::skin_agent = true;
                SkinChanger::SetCustomAgentModel(team, path == selected ? std::string_view{} : path);
            }
        }
        ImGui::EndChild();
    }

    else if (g_page == Page_Browser) {
        // Browser for Agents
        if (g_sub == Agents) {
            const int team = g_browsingAgentTeam;
            const bool usingCustom = !SkinChanger::CustomAgentModel(team).empty();
            std::vector<SkinItems::Item*> teamAgents;
            for (auto& it : all) {
                if (it.type == SkinItems::Agent && it.team == team) {
                    if (NameMatch(it.name.c_str(), g_search))
                        teamAgents.push_back(&it);
                }
            }

            // Header Bar: Back Button & Search
            if (UiText::Button("< Back to Teams", ImVec2(120.f * dpi, 28.f * dpi))) {
                g_page = Page_Grid;
                g_search[0] = 0;
            }
            ImGui::SameLine(0.f, 12.f * dpi);
            ImGui::SetNextItemWidth((std::max)(120.f * dpi, ImGui::GetContentRegionAvail().x));
            UiText::InputTextWithHint("##AgentSearch", "Search agent name...", g_search, sizeof(g_search));
            // Keep the result count below the field at narrow widths.
            UiText::TextDisabled("%s Models (%d total)",
                                 (team == 2) ? "Terrorist" : "Counter-Terrorist",
                                 static_cast<int>(teamAgents.size()));

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::BeginChild("##AgentBrowserGrid", ImVec2(0.f, 0.f), ImGuiChildFlags_None);
            {
                const GridLayout grid = MakeGridLayout(ImGui::GetContentRegionAvail().x);
                const float spacing = grid.gap;
                const float cardW = grid.cardW;
                const float cardH = (std::max)(grid.cardH, 122.f * dpi);
                const float round = std::clamp(Config::menu_rounding, 2.f, 6.f) * dpi;
                const int nCols = grid.columns;

                for (int i = 0; i < static_cast<int>(teamAgents.size()); ++i) {
                    auto* ag = teamAgents[i];
                    if (!ag)
                        continue;

                    ImGui::PushID(ag->def);
                    if (i % nCols != 0)
                        ImGui::SameLine(0.f, spacing);

                    const int curEquipped = (team == 2) ? Config::skin_agent_t : Config::skin_agent_ct;
                    const bool isEquipped =
                        (!usingCustom && ag->def == static_cast<uint16_t>(curEquipped) && Config::skin_agent);

                    if (UiText::Selectable("##agent_card", false, 0, ImVec2(cardW, cardH))) {
                        if (isEquipped) {
                            if (team == 2)
                                Config::skin_agent_t = 0;
                            else
                                Config::skin_agent_ct = 0;
                        } else {
                            Config::skin_agent = true;
                            if (team == 2)
                                Config::skin_agent_t = ag->def;
                            else
                                Config::skin_agent_ct = ag->def;
                        }
                        SkinChanger::SetCustomAgentModel(team, {});
                    }

                    const ImVec2 rmin = ImGui::GetItemRectMin();
                    const ImVec2 rmax = ImGui::GetItemRectMax();
                    const bool isHovered = ImGui::IsItemHovered();
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    DrawCardChrome(dl, rmin, rmax, isEquipped, isHovered, round);

                    ImTextureID tex = (ImTextureID)0;
                    if (!ag->icon.empty())
                        tex = preview.GetTexture(SkinPreview::AgentPath(ag->icon.c_str()));

                    const float imgH = cardH - (ImGui::GetFontSize() + 18.f * dpi);
                    const ImVec2 imgMin(rmin.x + 8.f * dpi, rmin.y + 6.f * dpi);
                    const ImVec2 imgMax(rmax.x - 8.f * dpi, rmin.y + imgH);
                    DrawImageWell(dl, imgMin, imgMax, tex, 1.0f, round - 1.f * dpi);

                    DrawTextCentered(ag->name.c_str(),
                                     ImVec2(rmin.x + 6.f * dpi, rmin.y + imgH + 8.f * dpi),
                                     cardW - 12.f * dpi,
                                     MenuUI::ToU32(MenuUI::TextBright()));

                    if (isEquipped)
                        DrawBadgeCheck(dl, ImVec2(rmax.x - 18.f * dpi, rmin.y + 4.f * dpi), 14.f * dpi);

                    ImGui::PopID();
                }
            }
            ImGui::EndChild();
            return;
        }

        // Weapon / Knife / Glove Skin Browser
        auto* weapon = mgr.Find(g_browsingDef);
        if (!weapon) {
            g_page = Page_Grid;
            return;
        }

        if (!weapon->skinsReady)
            mgr.EnsureSkins(weapon->def);

        const int curEquippedPaint = AppliedPaint(g_sub, weapon->def);

        // Header Bar: Back Button, Title, Search
        ImGui::BeginGroup();
        if (UiText::Button("< Back to Inventory", ImVec2(140.f * dpi, 28.f * dpi))) {
            g_page = Page_Grid;
            g_search[0] = 0;
        }
        ImGui::SameLine(0.f, 12.f * dpi);
        ImGui::SetNextItemWidth((std::max)(120.f * dpi, ImGui::GetContentRegionAvail().x));
        UiText::InputTextWithHint("##SkinSearch", "Search skins...", g_search, sizeof(g_search));
        // The title/count row gets its own line so it cannot overlap the search field.
        UiText::TextColored(ImVec4(0.95f, 0.95f, 0.95f, 1.f), "%s", weapon->name.c_str());
        ImGui::SameLine(0.f, 8.f * dpi);
        UiText::TextDisabled("%d Available Skins", static_cast<int>(weapon->skins.size()));
        ImGui::EndGroup();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        int clickedPaint = -1;
        ImVec2 clickedMin{}, clickedMax{};
        std::vector<int> filteredSkins;
        for (int si = 0; si < static_cast<int>(weapon->skins.size()); ++si) {
            if (NameMatch(weapon->skins[si].name.c_str(), g_search))
                filteredSkins.push_back(si);
        }

        // Skins Grid (5 Columns of Large Skin Cards with Aspect-Fit Image)
        ImGui::BeginChild("##MaycrySkinsGridChild", ImVec2(0.f, 0.f), ImGuiChildFlags_None);
        {
            const GridLayout grid = MakeGridLayout(ImGui::GetContentRegionAvail().x);
            const float spacing = grid.gap;
            const float cardW = grid.cardW;
            const float cardH = grid.cardH;
            const float round = std::clamp(Config::menu_rounding, 2.f, 6.f) * dpi;
            const int nCols = grid.columns;

            // Vanilla / Stock - IDA verified panorama base_weapons/melee fallback
            bool _showVanilla = !g_search[0] || NameMatch("vanilla", g_search) || NameMatch("stock", g_search) ||
                                NameMatch("default", g_search);
            int _vanillaCols = _showVanilla ? 1 : 0;
            if (_showVanilla) {
                ImGui::PushID(-1);
                const bool vanillaEquipped = (curEquippedPaint == 0);
                if (UiText::Selectable("##skin_vanilla", false, 0, ImVec2(cardW, cardH))) {
                    clickedPaint = 0;
                    clickedMin = ImGui::GetItemRectMin();
                    clickedMax = ImGui::GetItemRectMax();
                }
                const ImVec2 rmin = ImGui::GetItemRectMin();
                const ImVec2 rmax = ImGui::GetItemRectMax();
                const bool isHovered = ImGui::IsItemHovered();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                DrawCardChrome(dl, rmin, rmax, vanillaEquipped, isHovered, round);
                ImTextureID vtex = (ImTextureID)0;
                if (!weapon->simple.empty())
                    vtex = preview.GetModelTexture(weapon->simple.c_str());
                const float imgH = grid.imageBottom;
                const ImVec2 imgMin(rmin.x + 8.f * dpi, rmin.y + 6.f * dpi);
                const ImVec2 imgMax(rmax.x - 8.f * dpi, rmin.y + imgH);
                DrawImageWell(dl, imgMin, imgMax, vtex, 1.45f, round - 1.f * dpi);
                const ImU32 rCol = GetRarityColor(0);
                dl->AddRectFilled(ImVec2(rmin.x + 6.f * dpi, rmin.y + imgH + 2.f * dpi),
                                  ImVec2(rmax.x - 6.f * dpi, rmin.y + imgH + 4.f * dpi),
                                  rCol,
                                  1.f);
                DrawTextCentered(
                    "Vanilla", ImVec2(rmin.x + 6.f * dpi, rmin.y + imgH + 7.f * dpi), cardW - 12.f * dpi, rCol);
                if (vanillaEquipped)
                    DrawBadgeCheck(dl, ImVec2(rmax.x - 18.f * dpi, rmin.y + 4.f * dpi), 14.f * dpi);
                if (isHovered)
                    UiText::SetTooltip(g_sub == Knives ? "Vanilla\nKeep this knife model, remove the skin"
                                                       : "Vanilla\nNo paint - base model\n(Click to configure)");
                ImGui::PopID();
            }
            for (int k = 0; k < static_cast<int>(filteredSkins.size()); ++k) {
                const int si = filteredSkins[k];
                auto& skin = weapon->skins[si];

                ImGui::PushID(skin.id);
                if ((k + _vanillaCols) % nCols != 0)
                    ImGui::SameLine(0.f, spacing);

                const bool isEquipped = (curEquippedPaint == skin.id);

                if (UiText::Selectable("##skin_card", false, 0, ImVec2(cardW, cardH))) {
                    clickedPaint = skin.id;
                    clickedMin = ImGui::GetItemRectMin();
                    clickedMax = ImGui::GetItemRectMax();
                }

                const ImVec2 rmin = ImGui::GetItemRectMin();
                const ImVec2 rmax = ImGui::GetItemRectMax();
                const bool isHovered = ImGui::IsItemHovered();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                DrawCardChrome(dl, rmin, rmax, isEquipped, isHovered, round);

                ImTextureID stex = (ImTextureID)0;
                if (!weapon->simple.empty() && !skin.token.empty())
                    stex = preview.GetPaintTexture(weapon->simple.c_str(), skin.token.c_str());

                const float imgH = grid.imageBottom;
                const ImVec2 imgMin(rmin.x + 8.f * dpi, rmin.y + 6.f * dpi);
                const ImVec2 imgMax(rmax.x - 8.f * dpi, rmin.y + imgH);
                DrawImageWell(dl, imgMin, imgMax, stex, 1.45f, round - 1.f * dpi);

                const ImU32 rCol = GetRarityColor(skin.rarity);
                dl->AddRectFilled(ImVec2(rmin.x + 6.f * dpi, rmin.y + imgH + 2.f * dpi),
                                  ImVec2(rmax.x - 6.f * dpi, rmin.y + imgH + 4.f * dpi),
                                  rCol,
                                  1.f);

                DrawTextCentered(
                    skin.name.c_str(), ImVec2(rmin.x + 6.f * dpi, rmin.y + imgH + 7.f * dpi), cardW - 12.f * dpi, rCol);

                // Checkmark Badge if Equipped
                if (isEquipped)
                    DrawBadgeCheck(dl, ImVec2(rmax.x - 18.f * dpi, rmin.y + 4.f * dpi), 14.f * dpi);

                if (isHovered)
                    UiText::SetTooltip("%s\nRarity: %s\nPaint ID: %d\n(Click to configure)",
                                       skin.name.c_str(),
                                       GetRarityName(skin.rarity),
                                       skin.id);

                ImGui::PopID();
            }
        }
        ImGui::EndChild();
        const float popupWidth = (std::min)(340.f * dpi, ImGui::GetIO().DisplaySize.x - 16.f);
        if (clickedPaint >= 0) {
            SyncActiveProperties(g_sub, weapon->def);
            g_editingPaint = clickedPaint;
            g_resetSkin = false;
            const auto display = ImGui::GetIO().DisplaySize;
            const float height = ImGui::GetFrameHeightWithSpacing() * 9.f + ImGui::GetTextLineHeightWithSpacing() * 6.f;
            const float x = clickedMax.x + 8.f * dpi + popupWidth <= display.x ? clickedMax.x + 8.f * dpi
                                                                               : clickedMin.x - popupWidth - 8.f * dpi;
            g_editorPosition = ImVec2(std::clamp(x, 8.f, (std::max)(8.f, display.x - popupWidth - 8.f)),
                                      std::clamp(clickedMin.y, 8.f, (std::max)(8.f, display.y - height - 8.f)));
            ImGui::OpenPopup("##SkinEditor");
        }
        ImGui::SetNextWindowPos(g_editorPosition, ImGuiCond_Appearing);
        ImGui::SetNextWindowSize(ImVec2(popupWidth, 0.f), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.f * dpi, 12.f * dpi));
        if (ImGui::BeginPopup("##SkinEditor")) {
            UiText::TextUnformatted(g_resetSkin           ? "Stock"
                                    : g_editingPaint == 0 ? "Vanilla"
                                                          : SkinDisplayName(weapon, g_editingPaint));
            ImGui::Separator();
            ImGui::BeginDisabled(g_resetSkin);
            ImVec4 wearCol;
            const char* tier = GetWearTier(g_wear, wearCol);
            UiText::TextUnformatted("Wear Condition");
            ImGui::SameLine();
            UiText::TextColored(wearCol, "%s", tier);
            ImGui::SetNextItemWidth(-1.f);
            if (UiText::InputFloat("##skin_wear", &g_wear, 0.f, 0.f, "%.4f"))
                g_wear = std::clamp(g_wear, 0.0001f, 1.f);
            UiText::TextUnformatted("Pattern Template / Seed");
            ImGui::SetNextItemWidth(-1.f);
            if (UiText::InputInt("##skin_seed", &g_seed, 0, 0))
                g_seed = std::clamp(g_seed, 0, 1000);
            UiText::Checkbox("StatTrak", &g_st);
            UiText::TextUnformatted("Count");
            ImGui::SetNextItemWidth(-1.f);
            if (UiText::InputInt("##skin_st_count", &g_stCount, 0, 0))
                g_stCount = (std::max)(g_stCount, 0);
            UiText::TextUnformatted("Name tag");
            ImGui::SetNextItemWidth(-1.f);
            UiText::InputTextWithHint("##skin_tag", "Custom Name Tag...", g_tag, sizeof(g_tag));
            ImGui::EndDisabled();
            if (UiText::Button("Reset to Stock", ImVec2(-1.f, 0.f)))
                g_resetSkin = true;
            for (int team : {2, 3}) {
                if (UiText::Button(team == 2 ? "Change for T" : "Change for CT", ImVec2(-1.f, 0.f))) {
                    if (g_resetSkin || (g_editingPaint == 0 && g_sub != Knives))
                        RemoveSkin(g_sub, weapon->def, team);
                    else
                        ApplySkin(g_sub, weapon->def, g_editingPaint, team);
                    g_skinTeam = team;
                    ImGui::CloseCurrentPopup();
                }
            }
            if (UiText::Button("Cancel", ImVec2(-1.f, 0.f)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar();
    }
}
