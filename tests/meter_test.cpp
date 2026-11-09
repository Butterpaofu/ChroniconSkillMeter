// Standalone unit test for the damage accounting core.
// No GameMaker / YYToolkit / Windows dependencies.
//
//   cl /std:c++20 /EHsc /I ..\ChroniconSkillMeter\source meter_test.cpp ^
//      ..\ChroniconSkillMeter\source\Meter.cpp /Fe:meter_test.exe
//
// (or add these two files to any C++20 project and run it)

#include "Meter.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

using namespace skillmeter;

static bool Near(double a, double b, double eps = 1e-6) {
    return std::fabs(a - b) < eps;
}

static void TestAggregation() {
    Meter m;
    // skill 10: 100 + 300 (one crit); skill 20: 50; skill 10 again: 200 (crit)
    m.Record(1000, 10, 100.0, false, "Ghoul");
    m.Record(1100, 10, 300.0, true,  "Ghoul");
    m.Record(1200, 20,  50.0, false, "Wraith");
    m.Record(1300, 10, 200.0, true,  "Wraith");

    auto rows = m.Rows(ViewMode::CurrentFight);
    assert(rows.size() == 2);
    assert(rows[0].skillId == 10);                 // sorted by total desc
    assert(Near(rows[0].total, 600.0));
    assert(rows[0].hits == 3 && rows[0].crits == 2);
    assert(Near(rows[0].critRate, 2.0 / 3.0));
    assert(Near(rows[0].maxHit, 300.0));
    assert(Near(rows[0].avgHit, 200.0));
    assert(Near(rows[0].share, 600.0 / 650.0));
    assert(Near(rows[1].total, 50.0));
    assert(Near(rows[1].share, 50.0 / 650.0));

    auto s = m.Summarize(ViewMode::CurrentFight, 1300);
    assert(Near(s.total, 650.0));
    assert(s.hits == 4 && s.skillCount == 2);
    assert(s.inCombat);

    auto targets = m.TargetBreakdown(ViewMode::CurrentFight, 10);
    assert(targets.size() == 2);
    assert(targets[0].first == "Ghoul" && Near(targets[0].second, 400.0));
}

static void TestFightSegmentation() {
    Options opt;
    opt.fightGapMs = 4000;
    Meter m(opt);
    m.Record(1000, 1, 100.0, false);
    m.Record(2000, 1, 100.0, false);
    m.Tick(3000);                                  // still in combat
    assert(m.FightCount() == 1);
    m.Record(9000, 2, 500.0, false);               // 7s gap -> new fight
    assert(m.FightCount() == 2);

    auto cur = m.Rows(ViewMode::CurrentFight);
    assert(cur.size() == 1 && cur[0].skillId == 2 && Near(cur[0].total, 500.0));

    auto prev = m.Rows(ViewMode::PreviousFight);
    assert(prev.size() == 1 && prev[0].skillId == 1 && Near(prev[0].total, 200.0));

    auto all = m.Rows(ViewMode::Overall);
    double total = 0.0;
    for (const auto& r : all) total += r.total;
    assert(Near(total, 700.0));

    m.EndFight();
    assert(m.History().size() == 2);
    // After the fight is archived, "Current" still shows the last fight.
    assert(Near(m.Rows(ViewMode::CurrentFight)[0].total, 500.0));

    m.Reset();
    assert(m.Rows(ViewMode::CurrentFight).empty());
    assert(m.FightCount() == 0);
}

static void TestWindowDpsAndSorting() {
    Options opt;
    opt.windowMs = 5000;
    Meter m(opt);
    m.Record(1000, 1, 1000.0, false);
    m.Record(2000, 2, 3000.0, false);
    assert(Near(m.WindowDps(2000), 800.0));        // 4000 / 5s
    assert(Near(m.WindowDps(7000), 600.0));        // the 1000 dmg hit expired
    m.Tick(8000);
    assert(Near(m.WindowDps(8000), 0.0));

    Meter s;
    s.Record(1000, 1, 10.0, false);
    s.Record(1000, 2, 20.0, false);
    s.Record(1000, 3, 30.0, true);
    auto byCrit = s.Rows(ViewMode::CurrentFight, SortMode::ByCritRate);
    assert(byCrit.front().skillId == 3);
    auto byHits = s.Rows(ViewMode::CurrentFight, SortMode::ByHits);
    assert(byHits.size() == 3);
}

static void TestFormattingAndCsv() {
    assert(Compact(999.0) == "999");
    assert(Compact(1500.0) == "1.50k");
    assert(Compact(1234567.0) == "1.23M");
    assert(Percent(0.125) == "12.5%");

    Meter m;
    m.Record(1000, 7, 100.0, true, "Dummy");
    const std::unordered_map<int, std::string> names{{7, "Fireball, Big"}};
    const std::string csv = m.Csv(ViewMode::CurrentFight, names);
    assert(csv.find("skill_id,skill_name") == 0);
    assert(csv.find("\"Fireball, Big\"") != std::string::npos);
}

int main() {
    TestAggregation();
    TestFightSegmentation();
    TestWindowDpsAndSorting();
    TestFormattingAndCsv();
    std::cout << "meter_test: all assertions passed\n";
    return 0;
}
