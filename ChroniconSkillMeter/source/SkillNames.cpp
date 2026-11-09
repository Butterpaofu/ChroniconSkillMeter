#include "SkillNames.hpp"
#include "Gml.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <vector>

namespace {

std::string Trim(const std::string& s) {
    const std::size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    const std::size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

bool LooksLikeAName(const std::string& s) {
    if (s.empty() || s.size() > 64) return false;
    // GML string() renders structs / ds_maps with brackets; skip those.
    if (s.find_first_of("[{}]") != std::string::npos) return false;
    if (s == "undefined" || s == "0" || s == "-1" || s == "pointer_null") return false;
    return true;
}

} // namespace

void SkillNames::Load(const std::wstring& path) {
    std::ifstream in(path);
    if (!in) return;
    std::string line;
    bool first = true;
    while (std::getline(in, line)) {
        if (first) {
            first = false;
            if (line.size() >= 3 &&
                static_cast<unsigned char>(line[0]) == 0xEF &&
                static_cast<unsigned char>(line[1]) == 0xBB &&
                static_cast<unsigned char>(line[2]) == 0xBF)
                line.erase(0, 3);
        }
        line = Trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        const std::size_t sep = line.find_first_of("=|");
        if (sep == std::string::npos) continue;
        try {
            const int id = std::stoi(Trim(line.substr(0, sep)));
            const std::string name = Trim(line.substr(sep + 1));
            if (!name.empty()) names_[id] = name;
        } catch (...) {
            // Ignore malformed lines rather than failing the whole mod.
        }
    }
}

void SkillNames::Learn(int id, const std::string& name) {
    if (id < -2 || !LooksLikeAName(name)) return;
    if (names_.find(id) == names_.end()) names_[id] = name;
    unknown_.erase(id);
}

std::string SkillNames::Get(int id) const {
    // actorDamage() passes -1 for damage with no skill (weapon hits, procs,
    // item effects). -2 is our own bucket for non-player sources.
    if (id == -1) return "Attack / Procs";
    if (id == -2) return "Minions / Other";
    auto it = names_.find(id);
    if (it != names_.end()) return it->second;
    unknown_.insert(id);
    return "Skill #" + std::to_string(id);
}

void SkillNames::ProbeGlobals() {
    if (!gml::Iface()) return;
    static const char* candidates[] = {
        "skillName", "skillNames", "skill_name", "skill_names",
        "skillDB", "skillData", "skillInfo", "skills",
        "global_skillName", "skillText", "skillTitle",
    };
    for (const char* key : candidates) {
        gml::RValue arr = gml::Call("variable_global_get", {gml::StrV(key)});
        if (!gml::Call("is_array", {arr}).AsBool()) continue;
        const std::size_t n = arr.length();
        if (n == 0 || n > 4096) continue;
        for (std::size_t i = 0; i < n; ++i) {
            const std::string name = gml::Text(arr[i]);
            if (LooksLikeAName(name)) Learn(static_cast<int>(i), name);
        }
    }
}

bool SkillNames::SaveUnknown(const std::wstring& path) const {
    if (unknown_.empty()) return false;
    std::ofstream out(path, std::ios::trunc | std::ios::binary);
    if (!out) return false;
    out << "# Skills seen without a name. Fill in the labels, then copy these\n"
           "# lines into skill_names.txt (UTF-8, latin glyphs only - the game\n"
           "# font used by the overlay cannot render CJK characters).\n";
    std::vector<int> ids(unknown_.begin(), unknown_.end());
    std::sort(ids.begin(), ids.end());
    for (int id : ids) out << id << "=\n";
    return true;
}
