#pragma once
// -----------------------------------------------------------------------------
// SkillNames.hpp - maps Chronicon's numeric skill ids to readable labels.
//
// Damage grouping NEVER depends on a name: stats are keyed by the skill id
// passed to actorDamage(). Names are only presentation. Unknown ids show as
// "Skill #N" and are written to unknown_skills.txt so they can be labelled
// later without losing any data.
// -----------------------------------------------------------------------------
#include <string>
#include <unordered_map>
#include <unordered_set>

class SkillNames {
public:
    // Plain UTF-8 "id=name" lines, '#' comments. Safe to call repeatedly.
    void Load(const std::wstring& path);
    void Learn(int id, const std::string& name);
    std::string Get(int id) const;

    // Best effort: look for global GML arrays holding skill names.
    // Never throws and never affects the statistics if nothing is found.
    void ProbeGlobals();

    // Writes every id seen without a name, ready to be filled in by the user.
    bool SaveUnknown(const std::wstring& path) const;

    const std::unordered_map<int, std::string>& Table() const { return names_; }
    std::size_t UnknownCount() const { return unknown_.size(); }

private:
    std::unordered_map<int, std::string> names_;
    mutable std::unordered_set<int> unknown_;
};
