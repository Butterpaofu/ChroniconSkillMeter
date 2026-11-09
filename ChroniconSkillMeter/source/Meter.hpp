#pragma once
// -----------------------------------------------------------------------------
// Meter.hpp - damage accounting core.
//
// Deliberately free of any GameMaker / YYToolkit / Windows dependency so it can
// be compiled and unit-tested on its own (see tests/meter_test.cpp).
// -----------------------------------------------------------------------------
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace skillmeter {

using TimeMs = std::uint64_t;

enum class ViewMode {
    CurrentFight,   // the fight in progress (or the last one once it ends)
    Overall,        // every fight recorded since the last reset
    PreviousFight,  // the last archived fight
};

enum class SortMode {
    ByTotal,
    ByDps,
    ByHits,
    ByCritRate,
};

struct Options {
    TimeMs fightGapMs = 4000;      // no damage for this long => the fight is over
    TimeMs windowMs = 5000;        // sliding window used by "recent DPS"
    std::size_t maxFights = 30;    // archived fights kept in memory
};

struct HitEvent {
    TimeMs time = 0;
    int skillId = -1;
    double amount = 0.0;
    bool crit = false;
    std::string target;
};

struct SkillAgg {
    int skillId = -1;
    double total = 0.0;
    std::uint64_t hits = 0;
    std::uint64_t crits = 0;
    double critDamage = 0.0;
    double maxHit = 0.0;
    double minHit = 0.0;
    TimeMs first = 0;
    TimeMs last = 0;
    std::unordered_map<std::string, double> byTarget;

    void Add(const HitEvent& e);
};

struct Fight {
    std::uint64_t id = 0;
    TimeMs start = 0;
    TimeMs lastHit = 0;
    TimeMs end = 0;                 // 0 while the fight is still active
    double total = 0.0;
    std::uint64_t hits = 0;
    std::unordered_map<int, SkillAgg> skills;

    bool Empty() const { return hits == 0; }
    TimeMs ActiveMs() const { return lastHit > start ? lastHit - start : 0; }
};

// One line of the Skada-style table. Names are resolved by the presentation
// layer (SkillNames), not here, so that statistics never depend on a name.
struct Row {
    int skillId = -1;
    double total = 0.0;
    double share = 0.0;             // 0..1
    double dps = 0.0;
    double critRate = 0.0;          // 0..1
    double maxHit = 0.0;
    double avgHit = 0.0;
    std::uint64_t hits = 0;
    std::uint64_t crits = 0;
    TimeMs first = 0;
    TimeMs last = 0;
};

struct Summary {
    double total = 0.0;
    double dps = 0.0;               // total / active fight time
    double windowDps = 0.0;         // damage in the last windowMs / window
    std::uint64_t hits = 0;
    std::size_t skillCount = 0;
    TimeMs activeMs = 0;
    bool inCombat = false;
    std::size_t fightCount = 0;
};

class Meter {
public:
    explicit Meter(Options opt = {});

    // Called once per damaging hit. `amount` must already be the *effective*
    // damage (HP actually removed), not the raw pre-mitigation roll.
    void Record(TimeMs now, int skillId, double amount, bool crit,
                std::string target = {});

    // Call once per frame so an idle fight can be archived.
    void Tick(TimeMs now);

    void Reset();                   // wipe current + history + window
    void EndFight();                // archive the current fight right now

    std::vector<Row> Rows(ViewMode view, SortMode sort = SortMode::ByTotal) const;
    Summary Summarize(ViewMode view, TimeMs now) const;
    double WindowDps(TimeMs now) const;

    // Breakdown of one skill by target name, sorted by damage descending.
    std::vector<std::pair<std::string, double>>
    TargetBreakdown(ViewMode view, int skillId) const;

    std::string Csv(ViewMode view,
                    const std::unordered_map<int, std::string>& names) const;

    const std::vector<Fight>& History() const { return history_; }
    const Fight& Current() const { return current_; }
    std::size_t FightCount() const { return history_.size() + (current_.Empty() ? 0 : 1); }
    const Options& GetOptions() const { return opt_; }

private:
    void Archive();
    const Fight* SelectFight(ViewMode view) const;
    std::unordered_map<int, SkillAgg> Aggregate(ViewMode view) const;
    TimeMs ActiveMsFor(ViewMode view,
                       const std::unordered_map<int, SkillAgg>& agg) const;

    Options opt_;
    Fight current_;
    std::vector<Fight> history_;
    std::deque<std::pair<TimeMs, double>> window_;
    std::uint64_t nextId_ = 0;
};

// Shared number formatting (also used by the overlay).
std::string Compact(double value);
std::string Percent(double fraction, int decimals = 1);
std::string Duration(TimeMs ms);

} // namespace skillmeter
