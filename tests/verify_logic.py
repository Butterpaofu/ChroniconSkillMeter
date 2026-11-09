# -*- coding: utf-8 -*-
"""Executable mirror of Meter.cpp's rules.

The local machine has no C++ compiler, so this script runs the exact same
specification (aggregation, fight segmentation, window DPS, shares, sorting)
and asserts identical numbers to tests/meter_test.cpp. Run:

    python tests/verify_logic.py
"""
from collections import defaultdict


def compact(v):
    suffixes = ["", "k", "M", "B", "T", "Qa", "Qi"]
    i = 0
    while abs(v) >= 1000 and i < 6:
        v /= 1000.0
        i += 1
    if i == 0:
        return f"{v:.0f}"
    if abs(v) >= 100:
        return f"{v:.0f}{suffixes[i]}"
    if abs(v) >= 10:
        return f"{v:.1f}{suffixes[i]}"
    return f"{v:.2f}{suffixes[i]}"


class Meter:
    def __init__(self, fight_gap_ms=4000, window_ms=5000, max_fights=30):
        self.fight_gap = fight_gap_ms
        self.window_ms = window_ms
        self.max_fights = max_fights
        self.current = None          # dict with start/last/total/hits/skills
        self.history = []
        self.window = []             # [(time, amount)]
        self.next_id = 0

    def _new_fight(self, now):
        self.next_id += 1
        return dict(id=self.next_id, start=now, last=now, end=0,
                    total=0.0, hits=0, skills=defaultdict(lambda: dict(
                        total=0.0, hits=0, crits=0, maxHit=0.0,
                        byTarget=defaultdict(float))))

    def record(self, now, skill_id, amount, crit=False, target=""):
        if not (amount > 0):
            return
        if self.current and now < self.current["last"]:
            now = self.current["last"]
        if self.current and now > self.current["last"] + self.fight_gap:
            self._archive()
        if self.current is None:
            self.current = self._new_fight(now)
        s = self.current["skills"][skill_id]
        s["total"] += amount
        s["hits"] += 1
        s["crits"] += int(crit)
        s["maxHit"] = max(s["maxHit"], amount)
        if target:
            s["byTarget"][target] += amount
        self.current["total"] += amount
        self.current["hits"] += 1
        self.current["last"] = now
        self.window.append((now, amount))
        self._trim(now)

    def tick(self, now):
        if self.current and now >= self.current["last"] and \
           now > self.current["last"] + self.fight_gap:
            self._archive()
        self._trim(now)

    def _trim(self, now):
        while self.window and now > self.window[0][0] + self.window_ms:
            self.window.pop(0)

    def _archive(self):
        if self.current is None:
            return
        self.current["end"] = self.current["last"]
        self.history.append(self.current)
        self.history = self.history[-self.max_fights:]
        self.current = None

    def end_fight(self):
        self._archive()

    def reset(self):
        self.current, self.history, self.window = None, [], []
        self.next_id = 0

    def fight_count(self):
        return len(self.history) + (1 if self.current else 0)

    def select(self, view):
        if view == "current":
            return self.current if self.current else (self.history[-1] if self.history else None)
        if view == "previous":
            skip = 0 if self.current else 1
            return self.history[-1 - skip] if len(self.history) > skip else None
        return None

    def aggregate(self, view):
        out = defaultdict(lambda: dict(total=0.0, hits=0, crits=0, maxHit=0.0,
                                       byTarget=defaultdict(float)))
        fights = self.history + ([self.current] if self.current else []) \
            if view == "overall" else [f for f in [self.select(view)] if f]
        for f in fights:
            for sid, s in f["skills"].items():
                d = out[sid]
                d["total"] += s["total"]
                d["hits"] += s["hits"]
                d["crits"] += s["crits"]
                d["maxHit"] = max(d["maxHit"], s["maxHit"])
                for t, v in s["byTarget"].items():
                    d["byTarget"][t] += v
        return out

    def active_ms(self, view, agg):
        if view == "overall":
            ms = sum(f["last"] - f["start"] for f in
                     self.history + ([self.current] if self.current else []))
        else:
            f = self.select(view)
            ms = (f["last"] - f["start"]) if f else 0
        return max(ms, 1000)          # avoid absurd DPS for a single hit

    def rows(self, view, sort="total"):
        agg = self.aggregate(view)
        total = sum(s["total"] for s in agg.values())
        seconds = self.active_ms(view, agg) / 1000.0
        out = []
        for sid, s in agg.items():
            out.append(dict(skillId=sid, total=s["total"],
                            share=(s["total"] / total) if total else 0.0,
                            dps=s["total"] / seconds, hits=s["hits"],
                            crits=s["crits"],
                            critRate=s["crits"] / s["hits"] if s["hits"] else 0.0,
                            maxHit=s["maxHit"],
                            avgHit=s["total"] / s["hits"] if s["hits"] else 0.0))
        keymap = {"total": "total", "dps": "dps", "hits": "hits", "crit": "critRate"}
        k = keymap[sort]
        return sorted(out, key=lambda r: (-r[k], r["skillId"]))

    def window_dps(self, now):
        s = sum(a for t, a in self.window if 0 <= now - t <= self.window_ms)
        return s / (self.window_ms / 1000.0)


def approx(a, b, eps=1e-6):
    assert abs(a - b) < eps, f"{a} != {b}"


def main():
    # --- same assertions as meter_test.cpp ---------------------------------
    m = Meter()
    m.record(1000, 10, 100, False, "Ghoul")
    m.record(1100, 10, 300, True, "Ghoul")
    m.record(1200, 20, 50, False, "Wraith")
    m.record(1300, 10, 200, True, "Wraith")
    rows = m.rows("current")
    assert len(rows) == 2 and rows[0]["skillId"] == 10
    approx(rows[0]["total"], 600)
    assert rows[0]["hits"] == 3 and rows[0]["crits"] == 2
    approx(rows[0]["critRate"], 2 / 3)
    approx(rows[0]["maxHit"], 300)
    approx(rows[0]["avgHit"], 200)
    approx(rows[0]["share"], 600 / 650)
    approx(sum(r["share"] for r in rows), 1.0)

    m2 = Meter()
    m2.record(1000, 1, 100)
    m2.record(2000, 1, 100)
    m2.tick(3000)
    assert m2.fight_count() == 1
    m2.record(9000, 2, 500)
    assert m2.fight_count() == 2
    approx(m2.rows("current")[0]["total"], 500)
    approx(m2.rows("previous")[0]["total"], 200)
    approx(sum(r["total"] for r in m2.rows("overall")), 700)
    m2.end_fight()
    assert len(m2.history) == 2
    approx(m2.rows("current")[0]["total"], 500)
    m2.reset()
    assert m2.fight_count() == 0

    m3 = Meter()
    m3.record(1000, 1, 1000)
    m3.record(2000, 2, 3000)
    approx(m3.window_dps(2000), 800)
    approx(m3.window_dps(7000), 600)
    m3.tick(8000)
    approx(m3.window_dps(8000), 0)

    assert compact(999) == "999"
    assert compact(1500) == "1.50k"
    assert compact(1234567) == "1.23M"

    print("verify_logic: all assertions passed (spec matches meter_test.cpp)\n")

    # --- sample table, so the numbers behind preview.html are real ----------
    demo = Meter()
    skills = [(101, "Frost Nova", 1_284_500, 143, 0.31),
              (204, "Chain Lightning", 982_300, 210, 0.22),
              (318, "Meteor", 741_800, 38, 0.44),
              (77, "Holy Bolt", 415_200, 97, 0.12),
              (-1, "Attack / Procs", 268_900, 312, 0.08),
              (512, "Blessed Hammer", 158_400, 64, 0.17)]
    t = 1000
    for idx, (sid, _name, total, hits, crit_rate) in enumerate(skills):
        for h in range(hits):
            t += 900 // len(skills) + 12
            amount = total / hits * (0.6 + 0.8 * ((h * 7 + idx) % 11) / 11)
            crit = (h % max(1, int(1 / crit_rate))) == 0
            demo.record(t, sid, amount, crit, ["Ghoul", "Wraith", "Bone Lord"][h % 3])
    total = sum(r["total"] for r in demo.rows("current"))
    print(f"{'SKILL':<20}{'DAMAGE':>12}{'SHARE':>9}{'DPS':>12}{'CRIT':>8}{'HITS':>7}")
    print("-" * 68)
    for r in demo.rows("current"):
        name = next(n for sid, n, *_ in skills if sid == r["skillId"])
        print(f"{name:<20}{compact(r['total']):>12}{r['share']*100:>8.1f}%"
              f"{compact(r['dps']):>12}{r['critRate']*100:>7.1f}%{r['hits']:>7}")
    print("-" * 68)
    print(f"{'TOTAL':<20}{compact(total):>12}{100.0:>8.1f}%"
          f"{compact(demo.window_dps(t)):>12}{'':>8}{sum(r['hits'] for r in demo.rows('current')):>7}")


if __name__ == "__main__":
    main()
