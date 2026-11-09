#include "Config.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace {

using Ini = std::unordered_map<std::string, std::string>;

std::string Trim(const std::string& s) {
    const std::size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    const std::size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

Ini ReadIni(const std::wstring& path) {
    Ini out;
    std::ifstream in(path);
    if (!in) return out;
    std::string line;
    bool first = true;
    while (std::getline(in, line)) {
        if (first) {                       // tolerate a UTF-8 BOM
            first = false;
            if (line.size() >= 3 &&
                static_cast<unsigned char>(line[0]) == 0xEF &&
                static_cast<unsigned char>(line[1]) == 0xBB &&
                static_cast<unsigned char>(line[2]) == 0xBF)
                line.erase(0, 3);
        }
        line = Trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        out[Trim(line.substr(0, eq))] = Trim(line.substr(eq + 1));
    }
    return out;
}

template <typename T>
void Get(const Ini& ini, const char* key, T& out) {
    auto it = ini.find(key);
    if (it == ini.end()) return;
    std::istringstream is(it->second);
    T value{};
    if (is >> value) out = value;
}

void GetBool(const Ini& ini, const char* key, bool& out) {
    auto it = ini.find(key);
    if (it == ini.end()) return;
    const std::string& v = it->second;
    out = (v == "1" || v == "true" || v == "yes" || v == "on");
}

void WriteDefaultIni(const std::wstring& path) {
    std::ofstream out(path);
    if (!out) return;
    out << "# ChroniconSkillMeter settings. Edit while the game is closed.\n"
           "# Coordinates are in a virtual 960x540 HUD space.\n\n"
           "[window]\n"
           "x = 18\ny = 96\nwidth = 420\nheight = 336\n"
           "rowHeight = 22\nmaxRows = 10\nvisible = 1\n"
           "view = 0        # 0 current fight, 1 overall, 2 previous fight\n"
           "sort = 0        # 0 damage, 1 dps, 2 hits, 3 crit rate\n\n"
           "[accounting]\n"
           "fightGapMs = 4000    # idle time that ends a fight\n"
           "windowMs = 5000      # sliding window for recent DPS\n"
           "maxFights = 30\n"
           "countMinions = 0\n"
           "exportCsv = 0\n"
           "learnSkillNames = 1\n\n"
           "[hotkeys]  # Windows VK codes: F5=116 F6=117 F7=118 F8=119 F9=120\n"
           "keyToggle = 116\nkeyReset = 117\nkeyView = 118\n"
           "keySort = 119\nkeyExport = 120\n"
           "mouseLeft = 1        # GameMaker mb_left\n";
}

std::wstring Join(const std::wstring& dir, const wchar_t* name) {
    if (dir.empty()) return name;
    const wchar_t last = dir.back();
    return dir + (last == L'\\' || last == L'/' ? L"" : L"\\") + name;
}

} // namespace

Config Config::Load(const std::wstring& moduleDir) {
    Config c;
    c.dir = moduleDir;
    c.iniPath = Join(moduleDir, L"skillmeter.ini");
    c.namesPath = Join(moduleDir, L"skill_names.txt");
    c.unknownPath = Join(moduleDir, L"unknown_skills.txt");
    c.csvPath = Join(moduleDir, L"damage_log.csv");

    std::ifstream probe(c.iniPath);
    if (!probe) WriteDefaultIni(c.iniPath);
    probe.close();

    const Ini ini = ReadIni(c.iniPath);
    Get(ini, "x", c.x);
    Get(ini, "y", c.y);
    Get(ini, "width", c.w);
    Get(ini, "height", c.h);
    Get(ini, "rowHeight", c.rowHeight);
    Get(ini, "maxRows", c.maxRows);
    Get(ini, "view", c.view);
    Get(ini, "sort", c.sort);
    GetBool(ini, "visible", c.visible);

    Get(ini, "fightGapMs", c.fightGapMs);
    Get(ini, "windowMs", c.windowMs);
    Get(ini, "maxFights", c.maxFights);
    GetBool(ini, "countMinions", c.countMinions);
    GetBool(ini, "exportCsv", c.exportCsv);
    GetBool(ini, "learnSkillNames", c.learnSkillNames);

    Get(ini, "keyToggle", c.keyToggle);
    Get(ini, "keyReset", c.keyReset);
    Get(ini, "keyView", c.keyView);
    Get(ini, "keySort", c.keySort);
    Get(ini, "keyExport", c.keyExport);
    Get(ini, "mouseLeft", c.mouseLeft);

    // Keep the window inside the HUD and the layout sane.
    if (c.w < 280) c.w = 280;
    if (c.w > 900) c.w = 900;
    if (c.h < 180) c.h = 180;
    if (c.h > 520) c.h = 520;
    if (c.rowHeight < 16) c.rowHeight = 16;
    if (c.rowHeight > 34) c.rowHeight = 34;
    if (c.maxRows < 3) c.maxRows = 3;
    if (c.maxRows > 20) c.maxRows = 20;
    if (c.view < 0 || c.view > 2) c.view = 0;
    if (c.sort < 0 || c.sort > 3) c.sort = 0;
    if (c.x < 0) c.x = 0;
    if (c.x > 900) c.x = 20;
    if (c.y < 0) c.y = 0;
    if (c.y > 500) c.y = 40;
    if (c.fightGapMs < 500) c.fightGapMs = 500;
    if (c.windowMs < 500) c.windowMs = 500;
    return c;
}

void Config::Save() const {
    std::ofstream out(iniPath, std::ios::trunc);
    if (!out) return;
    out << "# ChroniconSkillMeter settings (window state is saved on exit)\n"
        << "x = " << static_cast<int>(x) << "\n"
        << "y = " << static_cast<int>(y) << "\n"
        << "width = " << static_cast<int>(w) << "\n"
        << "height = " << static_cast<int>(h) << "\n"
        << "rowHeight = " << rowHeight << "\n"
        << "maxRows = " << maxRows << "\n"
        << "visible = " << (visible ? 1 : 0) << "\n"
        << "view = " << view << "\n"
        << "sort = " << sort << "\n"
        << "fightGapMs = " << fightGapMs << "\n"
        << "windowMs = " << windowMs << "\n"
        << "maxFights = " << maxFights << "\n"
        << "countMinions = " << (countMinions ? 1 : 0) << "\n"
        << "exportCsv = " << (exportCsv ? 1 : 0) << "\n"
        << "learnSkillNames = " << (learnSkillNames ? 1 : 0) << "\n"
        << "keyToggle = " << keyToggle << "\n"
        << "keyReset = " << keyReset << "\n"
        << "keyView = " << keyView << "\n"
        << "keySort = " << keySort << "\n"
        << "keyExport = " << keyExport << "\n"
        << "mouseLeft = " << mouseLeft << "\n";
}

skillmeter::Options Config::MeterOptions() const {
    skillmeter::Options o;
    o.fightGapMs = fightGapMs;
    o.windowMs = windowMs;
    o.maxFights = maxFights;
    return o;
}

skillmeter::ViewMode Config::View() const {
    switch (view) {
    case 1:  return skillmeter::ViewMode::Overall;
    case 2:  return skillmeter::ViewMode::PreviousFight;
    default: return skillmeter::ViewMode::CurrentFight;
    }
}

skillmeter::SortMode Config::Sort() const {
    switch (sort) {
    case 1:  return skillmeter::SortMode::ByDps;
    case 2:  return skillmeter::SortMode::ByHits;
    case 3:  return skillmeter::SortMode::ByCritRate;
    default: return skillmeter::SortMode::ByTotal;
    }
}
