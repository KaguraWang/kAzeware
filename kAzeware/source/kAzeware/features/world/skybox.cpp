#include "skybox.h"

#include "../../config/config.h"
#include "../../hooks/hooks.h"
#include "../../utils/memory/Interface/Interface.h"
#include "../../utils/memory/memsafe/memsafe.h"
#include "../../utils/memory/patternscan/patternscan.h"
#include "../sdk_prio_a/sdk_prio_a.h"
#include "../../../cs2/entity/C_Material/C_Material.h"

#include <Windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>

namespace World::Skybox {
    namespace {
        namespace fs = std::filesystem;

        struct BuiltIn {
            const char* material;
            const char* pack = nullptr;
            const char* texture = nullptr;
        };

        // Sky-shader materials from the shipped CS2 and Core packs, including
        // map-specific Valve-distributed packs. Props named "skybox" are excluded.
        constexpr BuiltIn kBuiltIns[] = {
            {"materials/de_bldr/hdri/bldr_sky.vmat_c",
             "csgo_community_addons/de_boulder/de_boulder_dir.vpk",
             "materials/de_bldr/hdri/bldr_sky_final_01_png_85db302.vtex_c"},
            {"materials/de_cache/sky/de_cache_sky_001.vmat_c"},
            {"materials/de_nuke/hr_nuke/sky/nuke_sky_hdr.vmat_c"},
            {"materials/de_overpass/skybox/overpass_skydome_1.vmat_c"},
            {"materials/dev/default_sky.vmat_c"},
            {"materials/editor/sky_default_grey.vmat_c"},
            {"materials/editor/toolscene_dust2_environment.vmat_c"},
            {"materials/editor/toolscene_dust2_environment_uppertunnel.vmat_c"},
            {"materials/editor/toolscene_nuke_environment.vmat_c"},
            {"materials/editor/toolscene_nuke_environment_2.vmat_c"},
            {"materials/editor/toolscene_lighting_sky_dust.vmat_c"},
            {"materials/poseidon_sky_01.vmat_c",
             "csgo_community_addons/de_poseidon/de_poseidon_dir.vpk",
             "rawmodels/3dskybox4k/poseidon_sky_03a_exr_421cef60.vtex_c"},
            {"materials/skybox/convoy_sky.vmat_c"},
            {"materials/skybox/cs_italy_s2_skybox_2.vmat_c"},
            {"materials/skybox/cs_italy_s2_skybox_2_fog.vmat_c"},
            {"materials/skybox/cs_italy_s2_skybox_2_lighting.vmat_c"},
            {"materials/skybox/cs_office_45_0.vmat_c"},
            {"materials/skybox/fachwerk.vmat_c",
             "csgo_community_addons/de_fachwerk/de_fachwerk_dir.vpk",
             "materials/skybox/fachwerk8_exr_94428a48.vtex_c"},
            {"materials/skybox/fachwerk_lighting.vmat_c",
             "csgo_community_addons/de_fachwerk/de_fachwerk_dir.vpk",
             "materials/skybox/fachwerk8_exr_94428a48.vtex_c"},
            {"materials/skybox/light_test_dark.vmat_c"},
            {"materials/skybox/light_test_default.vmat_c"},
            {"materials/skybox/light_test_interior_basic.vmat_c"},
            {"materials/skybox/light_test_interior_basic_2.vmat_c"},
            {"materials/skybox/light_test_interior_studio_basic.vmat_c"},
            {"materials/skybox/light_test_sky_default.vmat_c"},
            {"materials/skybox/light_test_sky_night_basic.vmat_c"},
            {"materials/skybox/light_test_sky_overcast.vmat_c"},
            {"materials/skybox/light_test_sky_sunny.vmat_c"},
            {"materials/skybox/light_test_sky_sunny02.vmat_c"},
            {"materials/skybox/light_test_sky_sunny03.vmat_c"},
            {"materials/skybox/light_test_sky_sunny_b.vmat_c"},
            {"materials/skybox/light_test_sky_sunny_basic.vmat_c"},
            {"materials/skybox/light_test_sky_sunset.vmat_c"},
            {"materials/skybox/light_test_sky_sunset_2.vmat_c"},
            {"materials/skybox/light_test_sky_sunset_light.vmat_c"},
            {"materials/skybox/light_test_sky_sunset_light_mesh.vmat_c"},
            {"materials/skybox/poolday_skybox.vmat_c",
             "csgo/maps/ar_pool_day.vpk",
             "materials/skybox/poolday_skybox_3_exr_12fe189a.vtex_c"},
            {"materials/skybox/rush_cloudy_01a.vmat_c"},
            {"materials/skybox/sh/sh_0.vmat_c"},
            {"materials/skybox/sh/sh_1.vmat_c"},
            {"materials/skybox/sh/sh_2.vmat_c"},
            {"materials/skybox/sh/sh_3.vmat_c"},
            {"materials/skybox/sh/sh_4.vmat_c"},
            {"materials/skybox/sh/sh_5.vmat_c"},
            {"materials/skybox/sh/sh_6.vmat_c"},
            {"materials/skybox/sh/sh_7.vmat_c"},
            {"materials/skybox/sh/sh_8.vmat_c"},
            {"materials/skybox/sky_ar_baggage_01.vmat_c"},
            {"materials/skybox/sky_black.vmat_c"},
            {"materials/skybox/sky_cs15_daylight01_hdr.vmat_c"},
            {"materials/skybox/sky_csgo_cloudy01.vmat_c"},
            {"materials/skybox/sky_de_annubis.vmat_c"},
            {"materials/skybox/sky_de_dust2.vmat_c"},
            {"materials/skybox/sky_de_dust2vmat.vmat_c"},
            {"materials/skybox/sky_de_mirage.vmat_c"},
            {"materials/skybox/sky_de_nuke.vmat_c"},
            {"materials/skybox/sky_de_overpass_01.vmat_c"},
            {"materials/skybox/sky_de_train03.vmat_c"},
            {"materials/skybox/sky_de_vertigo.vmat_c"},
            {"materials/skybox/sky_debris_overcast.vmat_c",
             "csgo_community_addons/de_debris/de_debris_dir.vpk",
             "materials/skybox/sky_debris_overcast_jpg_bc009966.vtex_c"},
            {"materials/skybox/sky_furnace.vmat_c"},
            {"materials/skybox/sky_hr_aztec_02.vmat_c"},
            {"materials/skybox/sky_hr_aztec_02_lighting.vmat_c"},
            {"materials/skybox/sky_hr_aztec_02_v1.vmat_c"},
            {"materials/skybox/sky_overcast_01.vmat_c"},
            {"materials/skybox/sky_rain_night_01.vmat_c"},
            {"materials/skybox/skybox_sunset_test.vmat_c",
             "csgo_community_addons/cs_shelter/cs_shelter_dir.vpk",
             "materials/skybox/belfast_sunset_puresky_2k_corrected_exr_f5d2fa09.vtex_c"},
            {"materials/skybox/skymodel_hosekwilkie_ref/skymodel_hosekwilkie_albedo_0_30_turbidity_02_0_elevation_55_0.vmat_c"},
            {"materials/skybox/skymodel_hosekwilkie_ref/skymodel_hosekwilkie_albedo_0_30_turbidity_02_0_elevation_65_0.vmat_c"},
            {"materials/skybox/test/s2_de_inferno_sky01.vmat_c"},
        };

        constexpr std::string_view kCustomFolder = "materials/skybox/custom/";
        constexpr std::string_view kMaterialSuffix = ".vmat_c";
        constexpr std::uint64_t kMaterialType = 0x74616d76; // vmat

        struct Binding {
            CMaterial2* material;
            std::byte padding[0x18];
            volatile LONG references;
        };

        static_assert(offsetof(Binding, references) == 0x20);

        struct ResourceName {
            std::uint32_t length = 0;
            std::uint32_t capacity = 0xc00000c8;
            char storage[200]{};
            std::uint64_t hash = 0;
            char extension[8]{};
        };

        static_assert(sizeof(ResourceName) == 0xe0);

        struct MaterialList {
            int count = 0;
            Binding** items = nullptr;
            int capacity = 0;
            std::uint32_t flags = 0;
        };

        using InitResourceName = bool(__fastcall*)(ResourceName*, const char*, std::uint64_t);
        using FindParameter = float*(__fastcall*)(CMaterial2*, const char*);
        using UpdateMaterial = void(__fastcall*)(CMaterial2*);
        using PurgeBuffer = void(__fastcall*)(ResourceName*, int);
        using FreeMemory = void(__cdecl*)(void*);

        void* g_resourceSystem = nullptr;
        void* g_filesystem = nullptr;
        InitResourceName g_initName = nullptr;
        FindParameter g_findParameter = nullptr;
        UpdateMaterial g_updateMaterial = nullptr;
        PurgeBuffer g_purge = nullptr;
        FreeMemory g_free = nullptr;

        std::mutex g_selectionMutex;
        std::string g_selection;
        std::atomic<unsigned> g_selectionRevision{};
        std::vector<Choice> g_choices;
        std::atomic<const char*> g_loadError{};
        std::atomic<Binding*> g_override{};
        fs::path g_packStage;
        std::string g_mountedStage;
        SRWLOCK g_drawLock = SRWLOCK_INIT;

        struct LayerTint {
            bool enabled = false;
            std::array<float, 3> rgb{};
            bool operator==(const LayerTint&) const = default;
        };

        struct SavedTint {
            Binding* binding = nullptr;
            std::array<float, 4> color{};
            bool sun = false;
        };

        std::vector<SavedTint> g_layerTints;
        unsigned g_appliedRevision = ~0u;
        std::uint32_t g_map = ~0u;
        LayerTint g_clouds{}, g_sun{};

        const fs::path& GameRoot() {
            static const fs::path root = [] {
                wchar_t path[MAX_PATH]{};
                const DWORD size = GetModuleFileNameW(nullptr, path, MAX_PATH);
                return size > 0 && size < MAX_PATH ? fs::path(path).parent_path().parent_path().parent_path()
                                                   : fs::path{};
            }();
            return root;
        }

        bool IsMaterialPath(std::string_view path) {
            return path.size() < 200 && path.starts_with("materials/") && path.ends_with(kMaterialSuffix) &&
                   path.find("..") == std::string_view::npos &&
                   path.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_-/ .") == std::string_view::npos;
        }

        const BuiltIn* FindBuiltIn(std::string_view material) {
            for (const auto& item : kBuiltIns) {
                if (material == item.material)
                    return &item;
            }
            return nullptr;
        }

        void ResolveNative() {
            if (g_resourceSystem)
                return;
            g_resourceSystem = I::Get<void>("resourcesystem.dll", "ResourceSystem013");
            g_filesystem = I::Get<void>("filesystem_stdio.dll", "VFileSystem017");
            const auto init = M::patternScan("materialsystem2", "41 B8 76 6D 61 74 E8 ? ? ? ? F7 44 24 20 FF FF FF 3F");
            const auto param = M::patternScan("materialsystem2", "E8 ? ? ? ? 4C 8B E8 4C 8B FD 48 85 C0");
            const auto update = M::patternScan("materialsystem2", "F6 41 ? 04 ? ? E8 ? ? ? ? 80 63 ? FB");
            if (init)
                g_initName = reinterpret_cast<InitResourceName>(M::getAbsoluteAddress(init + 6, 1, 0));
            if (param)
                g_findParameter = reinterpret_cast<FindParameter>(M::getAbsoluteAddress(param, 1, 0));
            if (update)
                g_updateMaterial = reinterpret_cast<UpdateMaterial>(M::getAbsoluteAddress(update + 6, 1, 0));
            if (const HMODULE tier0 = GetModuleHandleW(L"tier0.dll")) {
                g_purge = reinterpret_cast<PurgeBuffer>(GetProcAddress(tier0, "?Purge@CBufferString@@QEAAXH@Z"));
                g_free = reinterpret_cast<FreeMemory>(GetProcAddress(tier0, "MemAlloc_FreeFunc"));
            }
        }

        template <typename Fn> Fn Vfunc(void* object, int slot) {
            return reinterpret_cast<Fn>((*reinterpret_cast<void***>(object))[slot]);
        }

        void AddRef(Binding* binding) {
            InterlockedIncrement(&binding->references);
        }

        void Release(Binding* binding) {
            if (binding && InterlockedDecrement(&binding->references) == 0)
                Vfunc<void(__fastcall*)(void*, Binding*)>(g_resourceSystem, 2)(g_resourceSystem, binding);
        }

        bool CanOpenMaterial(const char* file) {
            if (void* handle = Vfunc<void*(__fastcall*)(void*, const char*, const char*, const char*)>(
                    g_filesystem, 13)(g_filesystem, file, "rb", "GAME")) {
                Vfunc<void(__fastcall*)(void*, void*)>(g_filesystem, 14)(g_filesystem, handle);
                return true;
            }
            return false;
        }

        Binding* LoadMaterial(const char* resource) {
            if (!g_resourceSystem || !g_initName || !g_purge)
                return nullptr;
            ResourceName name{};
            __try {
                if (!g_initName(&name, resource, kMaterialType))
                    return nullptr;
                auto* binding = Vfunc<Binding*(__fastcall*)(void*, ResourceName*, const char*)>(g_resourceSystem, 40)(
                    g_resourceSystem, &name, "");
                if (!binding || !binding->material)
                    return nullptr;
                const char* loadedName = binding->material->GetName();
                if (!loadedName || _stricmp(loadedName, resource) != 0)
                    return nullptr;
                AddRef(binding);
                return binding;
            } __finally {
                g_purge(&name, 0);
            }
        }

        bool StageFile(const fs::path& index,
                       std::string_view relative,
                       std::uint16_t archive,
                       std::uint64_t offset,
                       std::uint32_t bytes,
                       std::string_view preload) {
            std::error_code error;
            const auto destination = g_packStage / relative;
            if (fs::exists(destination, error))
                return true;

            auto source = index;
            if (archive != 0x7fff) {
                auto stem = index.stem().string();
                if (!stem.ends_with("_dir"))
                    return false;
                char suffix[16]{};
                snprintf(suffix, sizeof(suffix), "_%03u.vpk", static_cast<unsigned>(archive));
                source = index.parent_path() / (stem.substr(0, stem.size() - 4) + suffix);
            }
            std::ifstream input(source, std::ios::binary);
            if (!input || !input.seekg(offset))
                return false;

            fs::create_directories(destination.parent_path(), error);
            if (error)
                return false;
            std::ofstream output(destination, std::ios::binary);
            output.write(preload.data(), preload.size());
            std::array<char, 65536> buffer{};
            while (bytes && input && output) {
                const auto count = (std::min)(bytes, static_cast<std::uint32_t>(buffer.size()));
                if (!input.read(buffer.data(), count))
                    break;
                output.write(buffer.data(), count);
                bytes -= count;
            }
            output.close();
            if (!output || bytes) {
                fs::remove(destination, error);
                return false;
            }
            return true;
        }

        // Some official maps ship split packs that cannot be mounted as a loose
        // search path. Extract only the selected VMAT and its cubemap, on demand.
        bool StagePackSky(const BuiltIn& sky) {
            if (!sky.pack || !sky.texture)
                return false;
            const auto index = GameRoot() / sky.pack;
            std::ifstream file(index, std::ios::binary);
            std::uint32_t header[3]{};
            if (!file.read(reinterpret_cast<char*>(header), sizeof(header)) || header[0] != 0x55aa1234u ||
                (header[1] != 1 && header[1] != 2) || header[2] == 0 || header[2] > 32u * 1024u * 1024u)
                return false;
            const auto headerSize = header[1] == 2 ? 28u : 12u;
            file.seekg(headerSize);
            std::string tree(header[2], '\0');
            if (!file.read(tree.data(), tree.size()))
                return false;

            if (g_packStage.empty()) {
                std::error_code error;
                const auto temp = fs::temp_directory_path(error);
                if (error)
                    return false;
                const auto unique =
                    "kazeware-sky-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64());
                const auto stage = temp / unique;
                if (!fs::create_directory(stage, error))
                    return false;
                g_packStage = stage;
            }

            std::size_t cursor = 0;
            bool valid = true;
            auto next = [&]() -> std::string_view {
                const auto end = tree.find('\0', cursor);
                if (end == std::string::npos) {
                    valid = false;
                    return {};
                }
                const std::string_view result(tree.data() + cursor, end - cursor);
                cursor = end + 1;
                return result;
            };
            bool material = false, texture = false;
            for (auto extension = next(); valid && !extension.empty(); extension = next()) {
                for (auto folder = next(); valid && !folder.empty(); folder = next()) {
                    for (auto stem = next(); valid && !stem.empty(); stem = next()) {
                        constexpr std::size_t entrySize = 18;
                        if (tree.size() - cursor < entrySize)
                            return false;
                        std::uint16_t preload{}, archive{}, endMarker{};
                        std::uint32_t offset{}, bytes{};
                        const char* fields = tree.data() + cursor;
                        std::memcpy(&preload, fields + 4, sizeof(preload));
                        std::memcpy(&archive, fields + 6, sizeof(archive));
                        std::memcpy(&offset, fields + 8, sizeof(offset));
                        std::memcpy(&bytes, fields + 12, sizeof(bytes));
                        std::memcpy(&endMarker, fields + 16, sizeof(endMarker));
                        cursor += entrySize;
                        if (endMarker != 0xffff || tree.size() - cursor < preload)
                            return false;
                        const std::string_view prefix(tree.data() + cursor, preload);
                        cursor += preload;
                        if (extension != "vmat_c" && extension != "vtex_c")
                            continue;
                        std::string name = folder == " " ? "" : std::string(folder) + '/';
                        name += std::string(stem) + '.' + std::string(extension);
                        if (name != sky.material && name != sky.texture)
                            continue;
                        const std::uint64_t position =
                            offset + (archive == 0x7fff ? static_cast<std::uint64_t>(headerSize) + header[2] : 0);
                        if (!StageFile(index, name, archive, position, bytes, prefix))
                            return false;
                        (name == sky.material ? material : texture) = true;
                        if (material && texture)
                            return true;
                    }
                }
            }
            return false;
        }

        bool MaterialAvailable(const std::string& material) {
            if (!g_filesystem)
                return false;
            if (CanOpenMaterial(material.c_str()))
                return true;
            const auto* sky = FindBuiltIn(material);
            if (!sky || !StagePackSky(*sky))
                return false;
            if (g_mountedStage.empty()) {
                g_mountedStage = g_packStage.string();
                Vfunc<void(__fastcall*)(void*, const char*, const char*, int, int, int)>(g_filesystem, 31)(
                    g_filesystem, g_mountedStage.c_str(), "GAME", 1, 0, 0);
            }
            return CanOpenMaterial(material.c_str());
        }

        void Publish(Binding* replacement) {
            AcquireSRWLockExclusive(&g_drawLock);
            Binding* old = g_override.exchange(replacement, std::memory_order_acq_rel);
            ReleaseSRWLockExclusive(&g_drawLock);
            Release(old);
        }

        void WriteTint(Binding* binding, const float* color, std::size_t count) {
            if (auto* material = binding->material) {
                if (float* value = g_findParameter(material, "g_vColorTint")) {
                    std::copy_n(color, count, value);
                    g_updateMaterial(material);
                }
            }
        }

        void RestoreTint(const SavedTint& saved) {
            WriteTint(saved.binding, saved.color.data(), saved.color.size());
            Release(saved.binding);
        }

        void RestoreTints() {
            for (const auto& saved : g_layerTints)
                RestoreTint(saved);
            g_layerTints.clear();
        }

        void ApplyTints(const LayerTint& clouds, const LayerTint& sun) {
            if (!g_resourceSystem || !g_findParameter || !g_updateMaterial || !g_free)
                return;
            RestoreTints();
            if (!clouds.enabled && !sun.enabled)
                return;

            MaterialList loaded{};
            Vfunc<void(__fastcall*)(void*, std::uint64_t, MaterialList*, std::uint8_t)>(g_resourceSystem, 32)(
                g_resourceSystem, kMaterialType, &loaded, 7);
            for (int i = 0; i < loaded.count; ++i) {
                Binding* binding = loaded.items[i];
                auto* material = binding ? binding->material : nullptr;
                if (!material)
                    continue;
                const char* name = material->GetName();
                if (!name)
                    continue;
                const std::string_view path(name);
                const bool isSun = path.find("sun_disc") != std::string_view::npos;
                const bool isCloud = path.find("clouds") != std::string_view::npos;
                const LayerTint* tint = isSun ? &sun : isCloud ? &clouds : nullptr;
                if (!tint || !tint->enabled)
                    continue;
                float* value = g_findParameter(material, "g_vColorTint");
                if (!value || std::any_of(g_layerTints.begin(), g_layerTints.end(), [binding](const auto& saved) {
                        return saved.binding == binding;
                    }))
                    continue;
                g_layerTints.push_back({binding, {value[0], value[1], value[2], value[3]}, isSun});
                AddRef(binding);
                std::copy(tint->rgb.begin(), tint->rgb.end(), value);
                g_updateMaterial(material);
            }
            if (loaded.items && !(loaded.flags & 0xc0000000u))
                g_free(loaded.items);
        }

        void UpdateTints(const LayerTint& clouds, const LayerTint& sun, bool changedMap) {
            if (changedMap || (clouds.enabled && !g_clouds.enabled) || (sun.enabled && !g_sun.enabled)) {
                ApplyTints(clouds, sun);
                return;
            }
            std::erase_if(g_layerTints, [&](const SavedTint& saved) {
                const LayerTint& tint = saved.sun ? sun : clouds;
                const LayerTint& previous = saved.sun ? g_sun : g_clouds;
                if (!tint.enabled) {
                    RestoreTint(saved);
                    return true;
                }
                if (tint != previous)
                    WriteTint(saved.binding, tint.rgb.data(), tint.rgb.size());
                return false;
            });
        }

        LayerTint Tint(bool enabled, const ImVec4& color) {
            return {enabled,
                    {std::clamp(color.x, 0.f, 1.f), std::clamp(color.y, 0.f, 1.f), std::clamp(color.z, 0.f, 1.f)}};
        }
    } // namespace

    std::string Selected() {
        std::lock_guard lock(g_selectionMutex);
        return g_selection;
    }

    void Select(std::string_view material) {
        std::lock_guard lock(g_selectionMutex);
        if (!material.empty() &&
            (!IsMaterialPath(material) || (!FindBuiltIn(material) && !material.starts_with(kCustomFolder))))
            material = {};
        if (g_selection != material) {
            g_selection.assign(material);
            g_selectionRevision.fetch_add(1, std::memory_order_release);
        }
    }

    void RefreshChoices() {
        g_choices.clear();
        for (const auto& sky : kBuiltIns) {
            const std::string_view path(sky.material);
            const auto start = path.rfind('/') + 1;
            g_choices.push_back(
                {std::string(path.substr(start, path.size() - start - kMaterialSuffix.size())), sky.material});
        }
        const auto game = GameRoot() / "csgo";
        const auto firstCustom = g_choices.size();
        std::error_code error;
        for (fs::recursive_directory_iterator
                 i(game / kCustomFolder, fs::directory_options::skip_permission_denied, error),
             end;
             !error && i != end;
             i.increment(error)) {
            if (!i->is_regular_file(error))
                continue;
            const auto relative = i->path().lexically_relative(game).generic_string();
            auto material = relative;
            for (char& letter : material) {
                if (letter >= 'A' && letter <= 'Z')
                    letter += 'a' - 'A';
            }
            if (IsMaterialPath(material))
                g_choices.push_back({relative.substr(kCustomFolder.size(),
                                                     relative.size() - kCustomFolder.size() - kMaterialSuffix.size()),
                                     material});
        }
        std::sort(g_choices.begin() + firstCustom, g_choices.end(), [](const Choice& a, const Choice& b) {
            return a.label < b.label;
        });
    }

    const std::vector<Choice>& Choices() {
        if (g_choices.empty())
            RefreshChoices();
        return g_choices;
    }

    void Tick() {
        if (Config::loading.load(std::memory_order_acquire) || H::SessionMapLeaving() || !H::SessionLive())
            return;
        const auto revision = g_selectionRevision.load(std::memory_order_acquire);
        const auto map = SdkPrioA::MapGen();
        const bool changedMap = map != g_map;
        const auto clouds = Tint(Config::sky_cloud_tint, Config::sky_cloud_color);
        const auto sun = Tint(Config::sky_sun_tint, Config::sky_sun_color);
        if (!changedMap && revision == g_appliedRevision && clouds == g_clouds && sun == g_sun)
            return;

        if (clouds.enabled || sun.enabled)
            ResolveNative();
        if (changedMap || revision != g_appliedRevision) {
            const auto material = Selected();
            Binding* replacement = nullptr;
            if (!material.empty()) {
                ResolveNative();
                if (MaterialAvailable(material)) {
                    // The filesystem contains .vmat_c; the resource system looks up .vmat.
                    const std::string resource = material.substr(0, material.size() - 2);
                    replacement = LoadMaterial(resource.c_str());
                }
            }
            Publish(replacement);
            g_loadError.store(!replacement && !material.empty() ? "Skybox material could not be loaded" : nullptr,
                              std::memory_order_release);
            g_appliedRevision = revision;
        }
        if (changedMap || clouds != g_clouds || sun != g_sun)
            UpdateTints(clouds, sun, changedMap);
        g_clouds = clouds;
        g_sun = sun;
        g_map = map;
    }

    void Shutdown() {
        Publish(nullptr);
        RestoreTints();
        if (!g_mountedStage.empty())
            Vfunc<bool(__fastcall*)(void*, const char*, const char*)>(g_filesystem,
                                                                      32)(g_filesystem, g_mountedStage.c_str(), "GAME");
        if (!g_packStage.empty()) {
            std::error_code error;
            fs::remove_all(g_packStage, error);
        }
    }

    const char* LoadError() {
        return g_loadError.load(std::memory_order_acquire);
    }

    bool HasOverride() {
        return g_override.load(std::memory_order_acquire) != nullptr;
    }

    void* AcquireOverride() {
        if (!g_override.load(std::memory_order_acquire))
            return nullptr;
        AcquireSRWLockExclusive(&g_drawLock);
        if (auto* binding = g_override.load(std::memory_order_relaxed))
            return binding;
        ReleaseSRWLockExclusive(&g_drawLock);
        return nullptr;
    }

    void ReleaseOverride() {
        ReleaseSRWLockExclusive(&g_drawLock);
    }
} // namespace World::Skybox
