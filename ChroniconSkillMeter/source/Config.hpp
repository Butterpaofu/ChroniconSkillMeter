#pragma once
// -----------------------------------------------------------------------------
// Config.hpp - user-tweakable settings, persisted to skillmeter.ini next to
// the DLL. All window coordinates are in the virtual 960x540 HUD space.
// -----------------------------------------------------------------------------
#include "Meter.hpp"

#include <cstdint>
#include <string>

struct Config {
    // Window
    double x = 18.0, y = 96.0, w = 420.0, h = 336.0;
    int rowHeight = 22;
    int maxRows = 10;
    bool visible = true;
    int view = 0;              // 0 = current fight, 1 = overall, 2 = previous
    int sort = 0;              // 0 = total, 1 = dps, 2 = hits, 3 = crit rate

    // Accounting
    std::uint32_t fightGapMs = 4000;
    std::uint32_t windowMs = 5000;
    std::uint32_t maxFights = 30;
    bool countMinions = false; // attribute non-player damage to "Minions / Other"
    bool exportCsv = false;    // append a CSV line each time a fight ends
    bool learnSkillNames = true;

    // Hotkeys: Windows VK_* values. F5..F9 by default.
    int keyToggle = 0x74;      // show / hide
    int keyReset = 0x75;       // wipe all data
    int keyView = 0x76;        // cycle view
    int keySort = 0x77;        // cycle sort
    int keyExport = 0x78;      // export CSV + unknown skill ids
    int mouseLeft = 1;         // GameMaker mb_left

    // Derived paths (populated by Load)
    std::wstring dir;
    std::wstring iniPath;
    std::wstring namesPath;
    std::wstring unknownPath;
    std::wstring csvPath;

    static Config Load(const std::wstring& moduleDir);
    void Save() const;
    skillmeter::Options MeterOptions() const;
    skillmeter::ViewMode View() const;
    skillmeter::SortMode Sort() const;
};
