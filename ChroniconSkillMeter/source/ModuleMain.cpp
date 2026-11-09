// -----------------------------------------------------------------------------
// ModuleMain.cpp - YYToolkit / Aurie entry point.
//
// Two hooks drive the whole mod:
//
//   gml_Script_actorDamage(dmg, crit, element, trigger, attacker, skill)
//       Self = the actor being hit, Args[4] = attacker, Args[5] = SKILL ID.
//       This is the single most useful hook: the game itself tells us which
//       skill produced the damage, so no time-window guessing is needed.
//
//   gml_Script_GUIdraw()
//       Per-frame HUD draw. We render the overlay after the original call so
//       the window stays on top of the game's own UI.
//
// Damage is measured as the HP actually removed from the target (before/after
// the original actorDamage call), which handles shields, overkill and
// mitigation correctly. Training dummies have an unusable HP value, so those
// fall back to the raw damage argument.
// -----------------------------------------------------------------------------
#include <YYToolkit/Shared.hpp>

#include "Config.hpp"
#include "Gml.hpp"
#include "Meter.hpp"
#include "Overlay.hpp"
#include "SkillNames.hpp"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

using namespace Aurie;
using namespace YYTK;

namespace {

PFUNC_YYGMLScript g_OrigDamage = nullptr;
PFUNC_YYGMLScript g_OrigGuiDraw = nullptr;

struct Context {
    Config cfg;
    skillmeter::Meter meter;
    SkillNames names;
    std::unique_ptr<Overlay> overlay;
    double playerRef = -1.0;
    unsigned frame = 0;
};
std::unique_ptr<Context> g_Ctx;

// --------------------------------------------------------------- damage hook

RValue& HookActorDamage(CInstance* Self, CInstance* Other, RValue& Result,
                        int Count, RValue** Args) {
    if (!g_OrigDamage) return Result;

    bool sample = false;
    double hpBefore = 0.0;
    double rawDamage = 0.0;
    int skillId = -1;
    bool crit = false;
    std::string target;

    if (g_Ctx && Count >= 5 && Self && Args && Args[0] && Args[4]) {
        const char* selfName =
            (Self->m_Object && Self->m_Object->m_Name) ? Self->m_Object->m_Name : "";
        const bool toPlayer = (std::strcmp(selfName, "obj_player") == 0);

        // Chronicon passes the attacker as an instance reference; GML's real()
        // turns it into the numeric id we compare against the player ref.
        const double attacker = gml::Call("real", {*Args[4]}).AsReal();
        const bool byPlayer =
            (g_Ctx->playerRef >= 0.0 && attacker == g_Ctx->playerRef);

        if (!toPlayer && (byPlayer || g_Ctx->cfg.countMinions)) {
            sample = true;
            hpBefore = gml::Call("variable_instance_get",
                                 {RValue(Self), RValue("hp")}).AsReal();
            rawDamage = gml::AsNum(Args[0]);
            const double rawSkillId = (Count >= 6 && Args[5])
                                          ? gml::AsNum(Args[5]) : -1.0;
            skillId = (std::isfinite(rawSkillId) && std::abs(rawSkillId) < 1e7)
                          ? static_cast<int>(rawSkillId) : -1;
            crit = (Count >= 2 && Args[1] && gml::AsNum(Args[1]) != 0.0);
            if (!byPlayer) skillId = -2;   // "Minions / Other" bucket

            RValue shown = gml::Call("variable_instance_get",
                                     {RValue(Self), RValue("name")});
            target = gml::Text(shown);
            if (target.empty() || target == "undefined" || target.size() > 64)
                target = selfName;
        }
    }

    RValue& ret = g_OrigDamage(Self, Other, Result, Count, Args);

    if (sample && g_Ctx) {
        const double hpAfter = gml::Call("variable_instance_get",
                                         {RValue(Self), RValue("hp")}).AsReal();
        const bool sane = std::abs(hpBefore) < 1e15 && std::abs(hpAfter) < 1e15;
        const double dealt = sane ? (std::max)(0.0, hpBefore - hpAfter)
                                  : (std::max)(0.0, rawDamage);
        if (dealt > 0.0) {
            g_Ctx->meter.Record(GetTickCount64(), skillId, dealt, crit, target);
        }
    }
    return ret;
}

// --------------------------------------------------- optional name-learning
//
// Chronicon has no documented "skill id -> name" API exposed to mods. If a
// script with a plausible name exists we hook it and passively learn labels
// whenever the game itself renders a skill name. This is strictly optional:
// grouping by id works perfectly without it.

struct NameProbe {
    const char* script;
    PFUNC_YYGMLScript orig;
};

NameProbe g_Probes[] = {
    {"gml_Script_skillGetName",   nullptr},
    {"gml_Script_skillName",      nullptr},
    {"gml_Script_skillGetTitle",  nullptr},
    {"gml_Script_getSkillName",   nullptr},
    {"gml_Script_skillGetNameByID", nullptr},
};
constexpr std::size_t kProbeCount = sizeof(g_Probes) / sizeof(g_Probes[0]);

template <std::size_t I>
RValue& ProbeHook(CInstance* Self, CInstance* Other, RValue& Result,
                  int Count, RValue** Args) {
    NameProbe& p = g_Probes[I];
    RValue& ret = p.orig ? p.orig(Self, Other, Result, Count, Args) : Result;
    if (g_Ctx && Count >= 1 && Args && Args[0]) {
        const int id = static_cast<int>(gml::AsNum(Args[0]));
        if (id >= 0 && g_Ctx->names.Table().find(id) == g_Ctx->names.Table().end()) {
            const std::string name = gml::Text(ret);
            if (!name.empty()) g_Ctx->names.Learn(id, name);
        }
    }
    return ret;
}

template <std::size_t... I>
void RegisterNameProbes(AurieModule* module, std::index_sequence<I...>) {
    (gml::Hook(module, g_Probes[I].script, &ProbeHook<I>,
               reinterpret_cast<PVOID*>(&g_Probes[I].orig)), ...);
}

// ------------------------------------------------------------------ HUD hook

RValue& HookGuiDraw(CInstance* Self, CInstance* Other, RValue& Result,
                    int Count, RValue** Args) {
    RValue& ret = g_OrigGuiDraw
                      ? g_OrigGuiDraw(Self, Other, Result, Count, Args) : Result;

    if (g_Ctx) {
        Context& c = *g_Ctx;
        // instance_find() is comparatively expensive; 20 frames is enough.
        if ((c.frame % 20) == 0) c.playerRef = gml::PlayerRef();
        ++c.frame;

        c.overlay->Frame(GetTickCount64());

        // Persist unlabelled skill ids periodically so names can be filled in
        // later even if the player never presses the export key.
        if ((c.frame % 1800) == 0) c.names.SaveUnknown(c.cfg.unknownPath);
    }
    return ret;
}

} // namespace

// ---------------------------------------------------------------- module init

EXPORTED AurieStatus ModuleInitialize(
    IN AurieModule* Module,
    IN const fs::path& ModulePath
) {
    YYTKInterface* iface = nullptr;
    if (!AurieSuccess(ObGetInterface("YYTK_Main", (AurieInterfaceBase*&)(iface))))
        return AURIE_MODULE_DEPENDENCY_NOT_RESOLVED;
    gml::Init(iface);

    const std::wstring dir = ModulePath.parent_path().wstring();

    auto ctx = std::make_unique<Context>();
    ctx->cfg = Config::Load(dir);
    ctx->meter = skillmeter::Meter(ctx->cfg.MeterOptions());
    ctx->names.Load(ctx->cfg.namesPath);
    ctx->overlay = std::make_unique<Overlay>(ctx->cfg, ctx->meter, ctx->names);
    ctx->playerRef = gml::PlayerRef();
    g_Ctx = std::move(ctx);

    const bool okDamage = gml::Hook(
        Module, "gml_Script_actorDamage", &HookActorDamage,
        reinterpret_cast<PVOID*>(&g_OrigDamage));
    const bool okDraw = gml::Hook(
        Module, "gml_Script_GUIdraw", &HookGuiDraw,
        reinterpret_cast<PVOID*>(&g_OrigGuiDraw));

    if (!okDamage)
        iface->PrintError(__FILE__, __LINE__,
                          "[SkillMeter] gml_Script_actorDamage not found - no data will be collected");
    if (!okDraw)
        iface->PrintError(__FILE__, __LINE__,
                          "[SkillMeter] gml_Script_GUIdraw not found - the window cannot be drawn");

    if (g_Ctx->cfg.learnSkillNames) {
        g_Ctx->names.ProbeGlobals();
        RegisterNameProbes(Module, std::make_index_sequence<kProbeCount>{});
    }

    iface->Print(CM_LIGHTGREEN,
                 "[SkillMeter] loaded - F5 window, F6 reset, F7 view, F8 sort, F9 export");
    return AURIE_SUCCESS;
}
