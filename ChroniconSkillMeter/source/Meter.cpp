#include "Meter.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <sstream>

namespace skillmeter {

// ---------------------------------------------------------------- SkillAgg

void SkillAgg::Add(const HitEvent& e) {
    skillId = e.skillId;
    total += e.amount;
    hits += 1;
    if (e.crit) {
        crits += 1;
        critDamage += e.amount;
    }
    if (maxHit < e.amount) maxHit = e.amount;
    if (hits == 1 || e.amount < minHit) minHit = e.amount;
    if (first == 0 || e.time < first) first = e.time;
    if (e.time > last) last = e.time;
    if (!e.target.empty()) byTarget[e.target] += e.amount;
}

// ------------------------------------------------------------------- Meter

Meter::Meter(Options opt) : opt_(opt) {}

void Meter::Record(TimeMs now, int skillId, double amount, bool crit,
                   std::string target) {
    if (!(amount > 0.0) || !std::isfinite(amount)) return;

    // GetTickCount64 never goes backwards, but a caller feeding a synthetic
    // clock (tests, replays) might. Clamp rather than produce negative spans.
    if (current_.lastHit && now < current_.lastHit) now = current_.lastHit;

    if (!current_.Empty() && now > current_.lastHit + opt_.fightGapMs) Archive();

    if (current_.Empty()) {
        current_ = Fight{};
        current_.id = ++nextId_;
        current_.start = now;
    }

    HitEvent e{now, skillId, amount, crit, std::move(target)};
    SkillAgg& agg = current_.skills[skillId];
    agg.Add(e);
    current_.total += amount;
    current_.hits += 1;
    current_.lastHit = now;

    window_.push_back({now, amount});
    while (!window_.empty() && now > window_.front().first + opt_.windowMs)
        window_.pop_front();
}

void Meter::Tick(TimeMs now) {
    if (current_.Empty()) return;
    if (now < current_.lastHit) return;
    if (now > current_.lastHit + opt_.fightGapMs) Archive();

    while (!window_.empty() && now > window_.front().first + opt_.windowMs)
        window_.pop_front();
}

void Meter::Archive() {
    if (current_.Empty()) return;
    current_.end = current_.lastHit;
    history_.push_back(std::move(current_));
    if (history_.size() > opt_.maxFights)
        history_.erase(history_.begin(),
                       history_.begin() + (history_.size() - opt_.maxFights));
    current_ = Fight{};
}

void Meter::EndFight() { Archive(); }

void Meter::Reset() {
    current_ = Fight{};
    history_.clear();
    window_.clear();
    nextId_ = 0;
}

const Fight* Meter::SelectFight(ViewMode view) const {
    if (view == ViewMode::CurrentFight) {
        if (!current_.Empty()) return &current_;
        // Skada keeps showing the finished fight until a new one starts.
        if (!history_.empty()) return &history_.back();
        return nullptr;
    }
    if (view == ViewMode::PreviousFight) {
        // "Previous" means the fight before the one currently on screen.
        const std::size_t skip = current_.Empty() ? 1 : 0;
        if (history_.size() > skip)
            return &history_[history_.size() - 1 - skip];
        return nullptr;
    }
    return nullptr; // Overall is built from every fight
}

std::unordered_map<int, SkillAgg> Meter::Aggregate(ViewMode view) const {
    std::unordered_map<int, SkillAgg> out;

    if (view == ViewMode::Overall) {
        auto merge = [&out](const Fight& f) {
            for (const auto& kv : f.skills) {
                SkillAgg& dst = out[kv.first];
                const SkillAgg& src = kv.second;
                if (dst.hits == 0) {
                    dst = src;
                } else {
                    dst.total += src.total;
                    dst.hits += src.hits;
                    dst.crits += src.crits;
                    dst.critDamage += src.critDamage;
                    dst.maxHit = (std::max)(dst.maxHit, src.maxHit);
                    dst.minHit = (std::min)(dst.minHit, src.minHit);
                    if (src.first < dst.first) dst.first = src.first;
                    if (src.last > dst.last) dst.last = src.last;
                    for (const auto& t : src.byTarget) dst.byTarget[t.first] += t.second;
                }
            }
        };
        for (const Fight& f : history_) merge(f);
        if (!current_.Empty()) merge(current_);
        return out;
    }

    if (const Fight* f = SelectFight(view)) return f->skills;
    return out;
}

TimeMs Meter::ActiveMsFor(ViewMode view,
                          const std::unordered_map<int, SkillAgg>& agg) const {
    TimeMs ms = 0;
    if (view == ViewMode::Overall) {
        for (const Fight& f : history_) ms += f.ActiveMs();
        ms += current_.ActiveMs();
    } else if (const Fight* f = SelectFight(view)) {
        ms = f->ActiveMs();
    }

    if (ms == 0) {
        // Fall back to the span covered by the hits themselves.
        TimeMs lo = (std::numeric_limits<TimeMs>::max)();
        TimeMs hi = 0;
        for (const auto& kv : agg) {
            lo = (std::min)(lo, kv.second.first);
            hi = (std::max)(hi, kv.second.last);
        }
        if (lo != (std::numeric_limits<TimeMs>::max)() && hi > lo) ms = hi - lo;
    }
    // A single hit would otherwise divide by zero and report an absurd DPS.
    return (std::max)(ms, static_cast<TimeMs>(1000));
}

std::vector<Row> Meter::Rows(ViewMode view, SortMode sort) const {
    const auto agg = Aggregate(view);
    std::vector<Row> rows;
    rows.reserve(agg.size());

    double total = 0.0;
    for (const auto& kv : agg) total += kv.second.total;
    const TimeMs activeMs = ActiveMsFor(view, agg);
    const double seconds = static_cast<double>(activeMs) / 1000.0;

    for (const auto& kv : agg) {
        const SkillAgg& s = kv.second;
        Row r;
        r.skillId = kv.first;
        r.total = s.total;
        r.share = total > 0.0 ? s.total / total : 0.0;
        r.dps = s.total / seconds;
        r.hits = s.hits;
        r.crits = s.crits;
        r.critRate = s.hits > 0 ? static_cast<double>(s.crits) / s.hits : 0.0;
        r.maxHit = s.maxHit;
        r.avgHit = s.hits > 0 ? s.total / s.hits : 0.0;
        r.first = s.first;
        r.last = s.last;
        rows.push_back(r);
    }

    auto key = [sort](const Row& r) {
        switch (sort) {
        case SortMode::ByDps:      return r.dps;
        case SortMode::ByHits:     return static_cast<double>(r.hits);
        case SortMode::ByCritRate: return r.critRate;
        default:                   return r.total;
        }
    };
    std::sort(rows.begin(), rows.end(), [&](const Row& a, const Row& b) {
        const double ka = key(a), kb = key(b);
        if (ka != kb) return ka > kb;              // descending
        return a.skillId < b.skillId;              // stable tie-break
    });
    return rows;
}

double Meter::WindowDps(TimeMs now) const {
    double sum = 0.0;
    for (const auto& h : window_)
        if (now >= h.first && now - h.first <= opt_.windowMs) sum += h.second;
    return sum / (static_cast<double>(opt_.windowMs) / 1000.0);
}

Summary Meter::Summarize(ViewMode view, TimeMs now) const {
    const auto agg = Aggregate(view);
    Summary s;
    s.skillCount = agg.size();
    s.activeMs = ActiveMsFor(view, agg);
    for (const auto& kv : agg) {
        s.total += kv.second.total;
        s.hits += kv.second.hits;
    }
    s.dps = s.total / (static_cast<double>(s.activeMs) / 1000.0);
    s.windowDps = WindowDps(now);
    s.fightCount = FightCount();
    s.inCombat = !current_.Empty() &&
                 (now >= current_.lastHit) &&
                 (now - current_.lastHit <= opt_.fightGapMs);
    return s;
}

std::vector<std::pair<std::string, double>>
Meter::TargetBreakdown(ViewMode view, int skillId) const {
    const auto agg = Aggregate(view);
    auto it = agg.find(skillId);
    std::vector<std::pair<std::string, double>> out;
    if (it == agg.end()) return out;
    out.reserve(it->second.byTarget.size());
    for (const auto& kv : it->second.byTarget) out.push_back(kv);
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
        if (a.second != b.second) return a.second > b.second;
        return a.first < b.first;
    });
    return out;
}

static std::string CsvCell(const std::string& in) {
    if (in.find_first_of(",\"\r\n") == std::string::npos) return in;
    std::string out = "\"";
    for (char c : in) {
        out += c;
        if (c == '"') out += '"';
    }
    out += '"';
    return out;
}

std::string Meter::Csv(ViewMode view,
                       const std::unordered_map<int, std::string>& names) const {
    std::ostringstream os;
    os << "skill_id,skill_name,total,share_pct,dps,hits,crits,crit_pct,max_hit,avg_hit\r\n";
    for (const Row& r : Rows(view, SortMode::ByTotal)) {
        auto it = names.find(r.skillId);
        const std::string name = it != names.end() ? it->second
                                                   : "Skill #" + std::to_string(r.skillId);
        os << r.skillId << ',' << CsvCell(name) << ','
           << r.total << ',' << (r.share * 100.0) << ',' << r.dps << ','
           << r.hits << ',' << r.crits << ',' << (r.critRate * 100.0) << ','
           << r.maxHit << ',' << r.avgHit << "\r\n";
    }
    return os.str();
}

// -------------------------------------------------------------- formatting

std::string Compact(double value) {
    static const char* suffixes[] = {"", "k", "M", "B", "T", "Qa", "Qi"};
    double v = value;
    double magnitude = std::fabs(v);
    int i = 0;
    while (magnitude >= 1000.0 && i < 6) {
        v /= 1000.0;
        magnitude /= 1000.0;
        ++i;
    }
    char buf[64];
    if (i == 0)
        std::snprintf(buf, sizeof(buf), "%.0f", v);
    else if (std::fabs(v) >= 100.0)
        std::snprintf(buf, sizeof(buf), "%.0f%s", v, suffixes[i]);
    else if (std::fabs(v) >= 10.0)
        std::snprintf(buf, sizeof(buf), "%.1f%s", v, suffixes[i]);
    else
        std::snprintf(buf, sizeof(buf), "%.2f%s", v, suffixes[i]);
    return buf;
}

std::string Percent(double fraction, int decimals) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f%%", decimals, fraction * 100.0);
    return buf;
}

std::string Duration(TimeMs ms) {
    const double seconds = static_cast<double>(ms) / 1000.0;
    char buf[64];
    if (seconds < 60.0) std::snprintf(buf, sizeof(buf), "%.1fs", seconds);
    else std::snprintf(buf, sizeof(buf), "%dm %02ds",
                       static_cast<int>(seconds) / 60,
                       static_cast<int>(seconds) % 60);
    return buf;
}

} // namespace skillmeter
