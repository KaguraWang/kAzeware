#include "../ragebot/rage.h"
#include "events.h"
#include "legitbot.h"
#include "../../hooks/hooks.h"
#include "../../utils/memory/memsafe/memsafe.h"
#include "../../utils/memory/patternscan/patternscan.h"
#include <Windows.h>
#include <atomic>

namespace Legitbot {
    namespace {
        struct Listener {
            void** vtable = nullptr;
            int debugId = 1;
        };

        using Add = bool(__fastcall*)(void*, Listener*, const char*, bool);
        using Remove = void(__fastcall*)(void*, Listener*);
        Listener listener{};
        void* vtable[3]{};
        void* manager = nullptr;
        std::atomic<bool> registered{false};

        void* __fastcall OnEvent(void*, void* event) {
            H::Hooks::EnterHook();
            __try {
                if (!H::Hooks::s_unloading.load(std::memory_order_acquire) && H::SessionEntityOk() &&
                    !H::SessionMapLeaving()) {
                    Rage::OnEvent(event);
                    if (!Rage::Active())
                        OnGameEvent(event);
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
            H::Hooks::LeaveHook();
            return nullptr;
        }

        int __fastcall DebugId(void* self) {
            return self ? static_cast<Listener*>(self)->debugId : 0;
        }

        bool AddEvent(Add add, const char* name) noexcept {
            __try {
                return add(manager, &listener, name, false);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        }

        void RemoveEvent(Remove remove) noexcept {
            __try {
                remove(manager, &listener);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }
    } // namespace

    bool InstallEvents() {
        const auto hit =
            M::FindPattern("client", "48 8B 0D ? ? ? ? 48 8D 15 ? ? ? ? 45 33 C9 45 33 C0 48 8B 01 FF 50 30");
        if (!hit)
            return false;
        auto** slot = reinterpret_cast<void**>(M::GetAbsoluteAddress(hit, 3));
        if (!slot || !Mem::IsReadable(slot, sizeof(void*)))
            return false;
        manager = Mem::ReadPtr(slot);
        void** table = nullptr;
        Add add = nullptr;
        Remove remove = nullptr;
        if (!manager || !Mem::ReadField(manager, 0, table) || !table ||
            !Mem::ReadField(table, 3 * sizeof(void*), add) || !add ||
            !Mem::ReadField(table, 5 * sizeof(void*), remove) || !remove) {
            manager = nullptr;
            return false;
        }
        vtable[0] = nullptr;
        vtable[1] = reinterpret_cast<void*>(&OnEvent);
        vtable[2] = reinterpret_cast<void*>(&DebugId);
        listener.vtable = vtable;
        if (!AddEvent(add, "weapon_fire")) {
            manager = nullptr;
            return false;
        }
        if (!AddEvent(add, "player_death")) {
            RemoveEvent(remove);
            manager = nullptr;
            return false;
        }
        registered.store(true, std::memory_order_release);
        return true;
    }

    void RemoveEvents() {
        if (!registered.exchange(false, std::memory_order_acq_rel) || !manager)
            return;
        void** table = nullptr;
        Remove remove = nullptr;
        if (Mem::ReadField(manager, 0, table) && table && Mem::ReadField(table, 5 * sizeof(void*), remove) && remove)
            RemoveEvent(remove);
        manager = nullptr;
    }

    bool EventsActive() {
        return registered.load(std::memory_order_acquire);
    }

    void* EventManager() {
        return manager;
    }
} // namespace Legitbot
