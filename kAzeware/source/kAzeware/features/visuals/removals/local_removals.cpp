#include "local_removals.h"
#include "../../../config/config.h"
#include "../../../utils/cvar/cvar.h"
#include "../../../hooks/hooks.h"
#include "../../panorama/panorama.h"
#include "../../../../cs2/entity/C_CSPlayerPawn/C_CSPlayerPawn.h"
#include <cstdio>
#include <Windows.h>
#include <string>

namespace LocalRemovals {
    namespace {
        struct Override {
            const char* name;
            int* value = nullptr;
            int saved = 0, written = 0;
            bool active = false;

            void Apply(bool enable, int replacement) {
                if (!enable) {
                    if (active && value && *value == written)
                        *value = saved;
                    active = false;
                    return;
                }
                if (!value)
                    value = Cvar::IntegerValue(name);
                if (!value)
                    return;
                if (!active || *value != written)
                    saved = *value;
                *value = replacement;
                written = replacement;
                active = true;
            }
        };

        Override hud{"cl_drawhud"}, radar{"cl_drawhud_force_radar"}, water{"r_csgo_water_effects"},
            particles{"r_drawparticles"};
        void* lastPanel = nullptr;
        int lastFlags = -1, lastSlot = -1, lastTeam = -1;
        ULONGLONG lastRefresh = 0;
        constexpr const char* nameScript = R"JS(
(function(){
 var root=$.GetContextPanel();
 var state=root.__kazewareOverhead;
 if(!state)state=root.__kazewareOverhead={generation:0};
 var generation=++state.generation;
 // Expire on the UI thread after DLL unload; no worker-thread V8 call.
 state.lease=Date.now()+1500;
 function apply(label,hide){
  if(!label||!label.IsValid())return;
  if(hide){
   if(label.__kazewareOverheadOwned===undefined){
    label.__kazewareOverheadOwned=true;
    label.__kazewareOverheadHidden=label.BHasClass('reticle--hidden');
   }
   label.AddClass('reticle--hidden');
  }else if(label.__kazewareOverheadOwned){
   if(!label.__kazewareOverheadHidden)label.RemoveClass('reticle--hidden');
   delete label.__kazewareOverheadOwned;delete label.__kazewareOverheadHidden;
  }
 }
 function tick(){
  if(!root.IsValid()||generation!==state.generation)return;
  var alive=Date.now()<=state.lease;
  var ids=root.FindChildrenWithClassTraverse('playerid');
  for(var i=0;i<ids.length;i++){
   var p=ids[i];
   var slot=parseInt($.Localize('{d:player_slot}',p),10);
   var local=slot===kazeSlot;
   var friendly=p.BHasClass(kazeTeam===2?'playerid--team-t':'playerid--team-ct');
   apply(p,alive&&isFinite(slot)&&((kazeSelf&&local)||(kazeFriendly&&friendly&&!local)));
  }
  if(alive&&(kazeSelf||kazeFriendly))$.Schedule(0.0,tick);
 }
 tick();
})()
)JS";

        bool Names(int flags, int slot, int team) {
            char values[128]{};
            snprintf(values,
                     sizeof(values),
                     "var kazeSelf=%s,kazeFriendly=%s,kazeSlot=%d,kazeTeam=%d;",
                     flags & 1 ? "true" : "false",
                     flags & 2 ? "true" : "false",
                     slot,
                     team);
            return Panorama::RunScriptHud((std::string(values) + nameScript).c_str());
        }
    } // namespace

    void Restore() {
        hud.Apply(false, 0);
        radar.Apply(false, 0);
        water.Apply(false, 0);
        particles.Apply(false, 0);
        if (lastPanel && lastFlags > 0 && !H::Hooks::s_unloading.load(std::memory_order_acquire))
            Names(0, lastSlot, lastTeam);
        lastPanel = nullptr;
        lastFlags = -1;
        lastSlot = lastTeam = -1;
    }

    void Update() {
        const bool enabled = Config::local_visuals;
        hud.Apply(enabled && Config::remove_hud, 0);
        radar.Apply(enabled && (Config::remove_hud || Config::remove_radar), Config::remove_radar ? -1 : 1);
        water.Apply(enabled && Config::remove_water, 0);
        particles.Apply(enabled && Config::remove_particles, 0);
        const int flags =
            enabled ? (int(Config::remove_local_overhead) | int(Config::remove_friendly_overhead) << 1) : 0;
        if (!flags && lastFlags <= 0)
            return;
        auto* local = H::SafeLocalPlayer();
        auto* panel = Panorama::HudPanel();
        if (!panel)
            return;
        const int slot = local ? local->m_hController().index() - 1 : -1, team = local ? local->m_iTeamNum() : 0;
        const auto now = GetTickCount64();
        if (panel == lastPanel && flags == lastFlags && slot == lastSlot && team == lastTeam) {
            if (flags && now - lastRefresh >= 500 &&
                Panorama::RunScriptHud(
                    "(function(){var s=$.GetContextPanel().__kazewareOverhead;if(s)s.lease=Date.now()+1500;})()"))
                lastRefresh = now;
            return;
        }
        if (Names(flags, slot, team)) {
            lastPanel = panel;
            lastFlags = flags;
            lastSlot = slot;
            lastTeam = team;
            lastRefresh = now;
        }
    }
} // namespace LocalRemovals
