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
//
// Lifecycle / diagnostics
// -----------------------
// With Aurie v1.0.5 the modules in mods\aurie are first all mapped, then all
// ModulePreinitialize routines are dispatched, and only later (after the game
// window becomes idle) all ModuleInitialize routines. A module whose
// ModuleInitialize returns a failure status is immediately purged/unloaded.
//
// Returning AURIE_MODULE_DEPENDENCY_NOT_RESOLVED just because YYTK_Main is not
// published YET is therefore fatal and completely silent. Instead we:
//   * always return AURIE_SUCCESS so Aurie keeps us loaded;
//   * write skillmeter.ini and skillmeter_diag.log at every stage;
//   * retry the interface + hook setup from a background thread.
// skillmeter_diag.log lives next to the DLL and is the single place to look
// when the overlay does not show up.
// -----------------------------------------------------------------------------
#include <YYToolkit/Shared.hpp>

#include "Config.hpp"
#include "Gml.hpp"
#include "Meter.hpp"
#include "Overlay.hpp"
#include "SkillNames.hpp"

#include <Windows.h>
#include <stdio.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <exception>
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

// ------------------------------------------------------------- diagnostics --
// Only kernel32 APIs are used here: this must also work from a global
// constructor (which runs while the loader lock is held).
std::wstring g_Dir;
std::atomic<bool> g_DirReady{false};
AurieModule* g_Module = nullptr;
std::atomic<bool> g_SetupStarted{false};
std::atomic<bool> g_SetupDone{false};
std::atomic<bool> g_RetryStarted{false};
std::atomic<bool> g_FirstFrameLogged{false};
std::atomic<bool> g_FirstHitLogged{false};
std::atomic<bool> g_ProbesDone{false};
HANDLE g_RetryThread = nullptr;

std::string Narrow(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(),
                                      static_cast<int>(w.size()),
                                      nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                        &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring ExeDir() {
    wchar_t buf[MAX_PATH] = {};
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (!n || n >= MAX_PATH) return {};
    const std::wstring p(buf, n);
    const std::size_t slash = p.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring{} : p.substr(0, slash);
}

std::wstring DiagPath() {
    // A global constructor calls Diag() before g_Dir's own dynamic
    // initialization; guard it so we never copy a half-built wstring.
    std::wstring base;
    if (g_DirReady.load(std::memory_order_acquire)) base = g_Dir;
    if (base.empty()) {
        const std::wstring exe = ExeDir();
        if (!exe.empty()) base = exe + L"\\mods\\aurie";
    }
    if (base.empty()) return L"skillmeter_diag.log";
    if (base.back() != L'\\' && base.back() != L'/') base += L'\\';
    return base + L"skillmeter_diag.log";
}

void SetDir(const fs::path& modulePath) {
    try {
        const std::wstring dir = modulePath.parent_path().wstring();
        g_Dir = dir;
        g_DirReady.store(true, std::memory_order_release);
    } catch (...) {
        g_DirReady.store(false);
    }
}

void Diag(const char* tag, const std::string& detail = {}) {
    char line[1200] = {};
    SYSTEMTIME st = {};
    GetLocalTime(&st);
    const int n = _snprintf_s(
        line, sizeof(line), _TRUNCATE,
        "[%02u:%02u:%02u.%03u] pid=%lu tid=%lu %-24s %s\r\n",
        static_cast<unsigned>(st.wHour), static_cast<unsigned>(st.wMinute),
        static_cast<unsigned>(st.wSecond), static_cast<unsigned>(st.wMilliseconds),
        static_cast<unsigned long>(GetCurrentProcessId()),
        static_cast<unsigned long>(GetCurrentThreadId()),
        tag, detail.c_str());
    if (n <= 0) return;

    HANDLE h = CreateFileW(DiagPath().c_str(), FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(h, line, static_cast<DWORD>(std::strlen(line)), &written, nullptr);
    CloseHandle(h);
}

struct AttachMarker {
    AttachMarker() { Diag("dll_process_attach", "module image mapped"); }
};
AttachMarker g_AttachMarker;

// -------------------------------------------------------------- name probes --

double PlayerObjectIndex() {
    static double idx = -2.0;
    if (idx == -2.0) idx = gml::Asset("obj_player");
    return idx;
}

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

    if (g_SetupDone.load(std::memory_order_acquire) && g_Ctx &&
        Count >= 5 && Self && Args && Args[0] && Args[4]) {
      try {
        // YYToolkit hides CInstance's layout behind YYTK_DEFINE_INTERNAL, and
        // that layout changes between GameMaker versions. Never dereference
        // Self->m_Object here: ask the engine through GML instead.
        const double selfObj = gml::Call(
            "variable_instance_get", {RValue(Self), RValue("object_index")}).AsReal();
        const double selfId = gml::Call("real", {RValue(Self)}).AsReal();
        const double playerObj = PlayerObjectIndex();
        const bool toPlayer =
            (playerObj >= 0.0 && selfObj == playerObj) ||
            (g_Ctx->playerRef >= 0.0 && selfId == g_Ctx->playerRef);

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
                target = "Enemy";
        }
      } catch (const std::exception& e) {
        Diag("damage_hook_exception", e.what());
        sample = false;
      } catch (...) {
        Diag("damage_hook_exception", "unknown");
        sample = false;
      }
    }

    RValue& ret = g_OrigDamage(Self, Other, Result, Count, Args);

    if (sample && g_Ctx) {
      try {
        const double hpAfter = gml::Call("variable_instance_get",
                                         {RValue(Self), RValue("hp")}).AsReal();
        const bool sane = std::abs(hpBefore) < 1e15 && std::abs(hpAfter) < 1e15;
        const double dealt = sane ? (std::max)(0.0, hpBefore - hpAfter)
                                  : (std::max)(0.0, rawDamage);
        if (dealt > 0.0) {
            g_Ctx->meter.Record(GetTickCount64(), skillId, dealt, crit, target);
            if (!g_FirstHitLogged.exchange(true)) {
                char b[160] = {};
                _snprintf_s(b, sizeof(b), _TRUNCATE,
                            "first damage: skill=%d amount=%.1f crit=%d target=%s",
                            skillId, dealt, crit ? 1 : 0, target.c_str());
                Diag("damage_hook_first_hit", b);
            }
        }
      } catch (...) {
        Diag("damage_post_exception", "unknown");
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

    if (!g_SetupDone.load(std::memory_order_acquire) || !g_Ctx) return ret;

    try {
        Context& c = *g_Ctx;
        if (!g_FirstFrameLogged.exchange(true)) {
            Diag("gui_hook_first_frame", "GUIdraw hook is alive");
        }

        // GML builtins must be called on the game thread, so this is the right
        // place for the very first player lookup and for the name probes.
        if ((c.frame % 20) == 0) c.playerRef = gml::PlayerRef();

        if (!g_ProbesDone.exchange(true) && c.cfg.learnSkillNames) {
            c.names.ProbeGlobals();
            RegisterNameProbes(g_Module, std::make_index_sequence<kProbeCount>{});
            Diag("name_probes", "registered on the game thread");
        }

        ++c.frame;
        c.overlay->Frame(GetTickCount64());

        // Persist unlabelled skill ids periodically so names can be filled in
        // later even if the player never presses the export key.
        if ((c.frame % 1800) == 0) c.names.SaveUnknown(c.cfg.unknownPath);
    } catch (const std::exception& e) {
        Diag("gui_hook_exception", e.what());
    } catch (...) {
        Diag("gui_hook_exception", "unknown");
    }
    return ret;
}

// ------------------------------------------------------------------- setup --
// Resolving the interface and installing the hooks is done exactly once, from
// whichever of ModuleInitialize / the retry thread gets a valid interface.
bool ResolveInterface(YYTKInterface*& out, char* text, std::size_t cap) {
    out = nullptr;
    const AurieStatus st = ObGetInterface(
        "YYTK_Main", reinterpret_cast<AurieInterfaceBase*&>(out));
    _snprintf_s(text, cap, _TRUNCATE, "status=0x%08X ptr=%p",
                static_cast<unsigned>(static_cast<int>(st)),
                reinterpret_cast<void*>(out));
    return AurieSuccess(st) && out != nullptr;
}

void DoSetup(bool verbose) {
    if (g_SetupDone.load(std::memory_order_acquire)) return;
    if (!g_DirReady.load(std::memory_order_acquire) || !g_Module) {
        if (verbose) Diag("setup_deferred", "module path or module handle unknown");
        return;
    }

    char stbuf[96] = {};
    YYTKInterface* iface = nullptr;
    if (!ResolveInterface(iface, stbuf, sizeof(stbuf))) {
        if (verbose) Diag("setup_deferred", std::string("YYTK_Main unavailable: ") + stbuf);
        return;
    }
    if (g_SetupStarted.exchange(true)) return;

    Diag("setup_begin", std::string(stbuf) + " dir=" + Narrow(g_Dir));
    try {
        gml::Init(iface);

        auto ctx = std::make_unique<Context>();
        ctx->cfg = Config::Load(g_Dir);
        ctx->meter = skillmeter::Meter(ctx->cfg.MeterOptions());
        ctx->names.Load(ctx->cfg.namesPath);
        ctx->overlay = std::make_unique<Overlay>(ctx->cfg, ctx->meter, ctx->names);
        // playerRef stays -1 until the first GUIdraw frame (game thread).
        g_Ctx = std::move(ctx);

        char b[160] = {};
        void* pDamage = gml::FindScript("gml_Script_actorDamage");
        void* pDraw = gml::FindScript("gml_Script_GUIdraw");
        _snprintf_s(b, sizeof(b), _TRUNCATE,
                    "actorDamage=%p GUIdraw=%p", pDamage, pDraw);
        Diag("script_lookup", b);

        const bool okDamage = gml::Hook(
            g_Module, "gml_Script_actorDamage", &HookActorDamage,
            reinterpret_cast<PVOID*>(&g_OrigDamage));
        const bool okDraw = gml::Hook(
            g_Module, "gml_Script_GUIdraw", &HookGuiDraw,
            reinterpret_cast<PVOID*>(&g_OrigGuiDraw));
        _snprintf_s(b, sizeof(b), _TRUNCATE,
                    "actorDamage hook=%d GUIdraw hook=%d",
                    okDamage ? 1 : 0, okDraw ? 1 : 0);
        Diag("hooks", b);

        if (!okDamage && iface)
            iface->PrintError(__FILE__, __LINE__,
                "[SkillMeter] gml_Script_actorDamage not found - no data will be collected");
        if (!okDraw && iface)
            iface->PrintError(__FILE__, __LINE__,
                "[SkillMeter] gml_Script_GUIdraw not found - the window cannot be drawn");

        g_SetupDone.store(true, std::memory_order_release);
        Diag("setup_done", "module is live");

        if (iface)
            iface->Print(CM_LIGHTGREEN,
                "[SkillMeter] loaded - F5 window, F6 reset, F7 view, F8 sort, F9 export");
    } catch (const std::exception& e) {
        Diag("setup_exception", e.what());
        g_SetupStarted.store(false);
    } catch (...) {
        Diag("setup_exception", "unknown");
        g_SetupStarted.store(false);
    }
}

DWORD WINAPI RetryThread(LPVOID) {
    // ArProcessAttach dispatches every ModulePreinitialize before it resumes
    // the game process. Give it time to publish YYTK_Main and finish its own
    // initialization before we start poking at the interface table.
    Sleep(5000);
    for (int i = 0; i < 600; ++i) {          // 600 * 250 ms = 150 s
        if (g_SetupDone.load(std::memory_order_acquire)) {
            Diag("retry_stop", "setup complete");
            return 0;
        }
        if (i % 20 == 0) {
            char b[64] = {};
            _snprintf_s(b, sizeof(b), _TRUNCATE, "attempt=%d", i);
            Diag("retry_poll", b);
        }
        DoSetup(false);
        Sleep(250);
    }
    Diag("retry_timeout", "YYTK_Main never became available");
    return 0;
}

void StartRetry() {
    if (g_RetryStarted.exchange(true)) return;
    HANDLE h = CreateThread(nullptr, 0, RetryThread, nullptr, 0, nullptr);
    if (!h) {
        Diag("retry_thread", "CreateThread failed");
        return;
    }
    g_RetryThread = h;   // intentionally kept open for the process lifetime
    Diag("retry_thread", "started");
}

void WriteConfigEarly() {
    if (!g_DirReady.load(std::memory_order_acquire)) return;
    try {
        Config probe = Config::Load(g_Dir);   // creates the default ini
        Diag("config_early", "ini=" + Narrow(probe.iniPath));
    } catch (const std::exception& e) {
        Diag("config_early_failed", e.what());
    } catch (...) {
        Diag("config_early_failed", "unknown");
    }
}

} // namespace

// ---------------------------------------------------------------- module init

EXPORTED AurieStatus ModulePreinitialize(
    IN AurieModule* Module,
    IN const fs::path& ModulePath
) {
    g_Module = Module;
    SetDir(ModulePath);
    Diag("module_preinitialize", "dir=" + Narrow(g_Dir));
    WriteConfigEarly();
    StartRetry();
    return AURIE_SUCCESS;
}

EXPORTED AurieStatus ModuleInitialize(
    IN AurieModule* Module,
    IN const fs::path& ModulePath
) {
    if (!g_Module) g_Module = Module;
    if (!g_DirReady.load(std::memory_order_acquire)) SetDir(ModulePath);
    Diag("module_initialize", "dir=" + Narrow(g_Dir));
    WriteConfigEarly();
    DoSetup(true);
    StartRetry();
    // Never report a dependency failure here: Aurie v1 purges any module whose
    // ModuleInitialize returns an error, with no log and no second chance.
    return AURIE_SUCCESS;
}

EXPORTED AurieStatus ModuleUnload(
    IN AurieModule*,
    IN const fs::path&
) {
    Diag("module_unload", "Aurie is unloading the module");
    return AURIE_SUCCESS;
}
