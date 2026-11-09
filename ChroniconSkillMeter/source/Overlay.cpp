#include "Overlay.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <sstream>

namespace {

// Chronicon's HUD fonts are latin-only, so every string drawn in-game is ASCII.
int C(int r, int g, int b) { return gml::Col(r, g, b); }
const int kPanel  = gml::Col(16, 20, 27);
const int kTitle  = gml::Col(26, 32, 42);
const int kBorder = gml::Col(66, 80, 100);
const int kText   = gml::Col(226, 233, 242);
const int kMuted  = gml::Col(132, 148, 168);
const int kAccent = gml::Col(96, 188, 250);
const int kDetail = gml::Col(11, 15, 21);
const int kHover  = gml::Col(48, 60, 76);
const int kLive   = gml::Col(108, 220, 140);
const int kBarBg  = gml::Col(30, 36, 46);

int BarColor(int skillId) {
    static const int palette[][3] = {
        {232, 124, 92}, {92, 186, 236}, {156, 204, 108}, {230, 186, 96},
        {178, 142, 236}, {98, 214, 182}, {236, 142, 182}, {140, 168, 216},
        {210, 206, 120}, {120, 206, 222},
    };
    const std::size_t i = std::hash<int>{}(skillId) % 10;
    return C(palette[i][0], palette[i][1], palette[i][2]);
}

const char* ViewLabel(int view) {
    switch (view) {
    case 1:  return "OVERALL";
    case 2:  return "PREVIOUS";
    default: return "CURRENT";
    }
}

const char* SortLabel(int sort) {
    switch (sort) {
    case 1:  return "DPS";
    case 2:  return "HITS";
    case 3:  return "CRIT";
    default: return "DAMAGE";
    }
}

// Layout constants in virtual pixels.
constexpr double kHeaderH = 26.0;
constexpr double kSummaryH = 30.0;
constexpr double kColumnsH = 18.0;
constexpr double kDetailH = 34.0;
constexpr double kFooterH = 20.0;

} // namespace

Overlay::Overlay(Config& cfg, skillmeter::Meter& meter, SkillNames& names)
    : cfg_(cfg), meter_(meter), names_(names) {}

void Overlay::Frame(std::uint64_t nowMs) {
    meter_.Tick(nowMs);

    const std::size_t archived = meter_.History().size();
    if (archived > lastArchived_) {
        if (cfg_.exportCsv) Export(nowMs, true);
        lastArchived_ = archived;
    } else if (archived < lastArchived_) {
        lastArchived_ = archived;    // after a reset
    }

    // Hotkeys work even while the window is hidden.
    if (gml::KeyPressed(cfg_.keyToggle)) cfg_.visible = !cfg_.visible;
    if (gml::KeyPressed(cfg_.keyReset)) ResetStats();
    if (gml::KeyPressed(cfg_.keyView)) { cfg_.view = (cfg_.view + 1) % 3; scroll_ = 0; }
    if (gml::KeyPressed(cfg_.keySort)) { cfg_.sort = (cfg_.sort + 1) % 4; scroll_ = 0; }
    if (gml::KeyPressed(cfg_.keyExport)) Export(nowMs);

    if (!cfg_.visible) return;

    gml::Canvas c;
    HandleInput(nowMs, c);
    Draw(nowMs, c);
}

void Overlay::Toggle() { cfg_.visible = !cfg_.visible; }

void Overlay::ResetStats() {
    meter_.Reset();
    scroll_ = 0;
    expanded_ = 0x7FFFFFFF;
    lastArchived_ = 0;
}

void Overlay::Export(std::uint64_t nowMs, bool quiet) {
    {
        std::ofstream out(cfg_.csvPath.c_str(), std::ios::trunc | std::ios::binary);
        if (out) out << meter_.Csv(cfg_.View(), names_.Table());
    }
    names_.SaveUnknown(cfg_.unknownPath);
    if (!quiet) toastUntil_ = nowMs + 2200;
}

void Overlay::HandleInput(std::uint64_t /*nowMs*/, const gml::Canvas& c) {
    const double gx = gml::Num("device_mouse_x_to_gui", {gml::NumV(0.0)});
    const double gy = gml::Num("device_mouse_y_to_gui", {gml::NumV(0.0)});
    mx_ = (gx - c.OX) / c.S;
    my_ = (gy - c.OY) / c.S;

    const gml::Rect window{cfg_.x, cfg_.y, cfg_.w, cfg_.h};
    const gml::Rect header{cfg_.x, cfg_.y, cfg_.w, kHeaderH};
    const gml::Rect columns{cfg_.x, cfg_.y + kHeaderH + kSummaryH,
                            cfg_.w, kColumnsH};

    const bool pressed = gml::MousePressed(cfg_.mouseLeft);
    const bool down = gml::MouseDown(cfg_.mouseLeft);

    if (window.Contains(mx_, my_)) {
        if (gml::WheelUp())   scroll_ = (std::max)(0, scroll_ - 1);
        if (gml::WheelDown()) scroll_ += 1;
    }

    if (pressed && window.Contains(mx_, my_)) {
        // View label (right side of the header)
        if (mx_ > cfg_.x + cfg_.w - 152 && mx_ < cfg_.x + cfg_.w - 62 &&
            my_ >= cfg_.y && my_ <= cfg_.y + kHeaderH) {
            cfg_.view = (cfg_.view + 1) % 3;
            scroll_ = 0;
            return;
        }
        // Close button
        if (mx_ > cfg_.x + cfg_.w - 24 && my_ <= cfg_.y + kHeaderH) {
            cfg_.visible = false;
            return;
        }
        // Sortable column headers
        if (columns.Contains(mx_, my_)) {
            if (mx_ > cfg_.x + cfg_.w * 0.72) cfg_.sort = 3;
            else if (mx_ > cfg_.x + cfg_.w * 0.60) cfg_.sort = (cfg_.sort == 1 ? 0 : 1);
            else cfg_.sort = 0;
            scroll_ = 0;
            return;
        }
        // Table rows
        const double tableTop = cfg_.y + kHeaderH + kSummaryH + kColumnsH;
        const double tableH = cfg_.h - kHeaderH - kSummaryH - kColumnsH
                              - kDetailH - kFooterH;
        const int visibleRows = (std::max)(1, static_cast<int>(tableH / cfg_.rowHeight));
        if (my_ >= tableTop && my_ < tableTop + tableH && mx_ < cfg_.x + cfg_.w - 8) {
            const int index = static_cast<int>((my_ - tableTop) / cfg_.rowHeight) + scroll_;
            const auto rows = meter_.Rows(cfg_.View(), cfg_.Sort());
            if (index >= 0 && index < static_cast<int>(rows.size())) {
                const int id = rows[static_cast<std::size_t>(index)].skillId;
                expanded_ = (expanded_ == id) ? 0x7FFFFFFF : id;
                return;
            }
        }
        // Title bar => start dragging
        if (header.Contains(mx_, my_)) {
            dragging_ = true;
            dragDx_ = mx_ - cfg_.x;
            dragDy_ = my_ - cfg_.y;
            return;
        }
    }

    if (dragging_) {
        if (down) {
            cfg_.x = mx_ - dragDx_;
            cfg_.y = my_ - dragDy_;
            cfg_.x = (std::max)(0.0, (std::min)(cfg_.x, 960.0 - cfg_.w));
            cfg_.y = (std::max)(0.0, (std::min)(cfg_.y, 540.0 - cfg_.h));
        } else {
            dragging_ = false;
            cfg_.Save();
        }
    }
}

void Overlay::Draw(std::uint64_t nowMs, const gml::Canvas& c) {
    const double x = cfg_.x, y = cfg_.y, w = cfg_.w, h = cfg_.h;

    // Frame ---------------------------------------------------------------
    c.Box({x, y, w, h}, kPanel, kBorder, 0.90);
    c.Box({x, y, w, kHeaderH}, kTitle, -1, 0.96);
    c.Fill({x, y + kHeaderH - 1, w, 1}, kBorder, 0.8);

    c.Text(x + 8, y + 8, "SKILL DAMAGE", kAccent, 1.05);
    const std::string viewText = std::string("[") + ViewLabel(cfg_.view) + "]";
    c.Text(x + w - 62, y + 8, viewText, kText, 0.95, 2);
    c.Text(x + w - 152, y + 8, SortLabel(cfg_.sort), kMuted, 0.85, 2);
    c.Text(x + w - 14, y + 8, "x", kMuted, 0.95, 2);
    if (nowMs < toastUntil_)
        c.Text(x + 128, y + 8, "EXPORTED", kLive, 0.9);

    // Summary -------------------------------------------------------------
    const auto sum = meter_.Summarize(cfg_.View(), nowMs);
    const double sy = y + kHeaderH;
    c.Fill({x + 1, sy, w - 2, kSummaryH - 2}, kBarBg, 0.55);
    c.Text(x + 8, sy + 5, "TOTAL", kMuted, 0.85);
    c.Text(x + 52, sy + 4, skillmeter::Compact(sum.total), kText, 1.15);
    c.Text(x + 8, sy + 18, "DPS", kMuted, 0.8);
    c.Text(x + 52, sy + 17, skillmeter::Compact(sum.dps), kText, 0.9);

    c.Text(x + w * 0.42, sy + 5, "LAST 5s", kMuted, 0.8);
    c.Text(x + w * 0.42, sy + 17, skillmeter::Compact(sum.windowDps) + "/s",
           kText, 0.95);
    c.Text(x + w * 0.68, sy + 5, "TIME", kMuted, 0.8);
    c.Text(x + w * 0.68, sy + 17, skillmeter::Duration(sum.activeMs), kText, 0.95);
    c.Text(x + w - 10, sy + 5, sum.inCombat ? "LIVE" : "IDLE",
           sum.inCombat ? kLive : kMuted, 0.9, 2);
    c.Text(x + w - 10, sy + 17,
           std::to_string(sum.skillCount) + " skills", kMuted, 0.8, 2);

    // Column headers ------------------------------------------------------
    const double cy = sy + kSummaryH;
    c.Fill({x + 1, cy, w - 2, kColumnsH - 1}, kTitle, 0.75);
    const double totalX = x + w * 0.60;
    const double shareX = x + w * 0.735;
    const double dpsX   = x + w * 0.865;
    const double critX  = x + w - 10;
    c.Text(x + 8,  cy + 4, "SKILL", kMuted, 0.85);
    c.Text(totalX, cy + 4, "DAMAGE", kMuted, 0.85, 2);
    c.Text(shareX, cy + 4, "SHARE", kMuted, 0.85, 2);
    c.Text(dpsX,   cy + 4, "DPS", kMuted, 0.85, 2);
    c.Text(critX,  cy + 4, "CRIT", kMuted, 0.85, 2);

    // Table ---------------------------------------------------------------
    const double tableTop = cy + kColumnsH;
    const double tableH = h - kHeaderH - kSummaryH - kColumnsH - kDetailH - kFooterH;
    const int visibleRows = (std::max)(1, static_cast<int>(tableH / cfg_.rowHeight));
    const auto rows = meter_.Rows(cfg_.View(), cfg_.Sort());

    const int maxScroll = (std::max)(0, static_cast<int>(rows.size()) - visibleRows);
    if (scroll_ > maxScroll) scroll_ = maxScroll;

    c.Fill({x + 1, tableTop, w - 2, tableH}, kPanel, 0.35);

    if (rows.empty()) {
        c.Text(x + w / 2, tableTop + 14, "No damage recorded yet", kMuted, 1.0, 1);
        c.Text(x + w / 2, tableTop + 34,
               "Hit something - each skill is listed separately", kMuted, 0.85, 1);
    }

    for (int i = 0; i < visibleRows; ++i) {
        const std::size_t index = static_cast<std::size_t>(i + scroll_);
        if (index >= rows.size()) break;
        const skillmeter::Row& r = rows[index];
        const double ry = tableTop + i * cfg_.rowHeight;
        const bool hovered = mx_ >= x && mx_ <= x + w && my_ >= ry &&
                             my_ < ry + cfg_.rowHeight;
        const bool selected = (expanded_ == r.skillId);

        if (hovered || selected) c.Fill({x + 2, ry, w - 4, cfg_.rowHeight}, kHover, 0.30);
        // Share bar sits behind the text, like Skada / Details!.
        const double barW = (w - 10) * (std::min)(1.0, r.share);
        c.Fill({x + 4, ry + 3, barW, cfg_.rowHeight - 6}, BarColor(r.skillId), 0.30);
        if (selected) c.Fill({x + 2, ry, 2, cfg_.rowHeight}, kAccent, 0.95);

        const double ty = ry + (cfg_.rowHeight - 12.0) / 2.0 + 2.0;
        const double nameMax = (totalX - x) - 34.0;
        c.Text(x + 9, ty, names_.Get(r.skillId), kText, 0.95, 0, nameMax);
        c.Text(totalX, ty, skillmeter::Compact(r.total), kText, 0.95, 2);
        c.Text(shareX, ty, skillmeter::Percent(r.share, 1), kMuted, 0.9, 2);
        c.Text(dpsX,   ty, skillmeter::Compact(r.dps), kMuted, 0.9, 2);
        c.Text(critX,  ty, skillmeter::Percent(r.critRate, 0), kMuted, 0.9, 2);
    }

    // Scrollbar -----------------------------------------------------------
    if (maxScroll > 0) {
        const double trackX = x + w - 5;
        c.Fill({trackX, tableTop, 3, tableH}, kBarBg, 0.8);
        const double thumbH = (std::max)(14.0, tableH * visibleRows /
                                                 static_cast<double>(rows.size()));
        const double thumbY = tableTop + (tableH - thumbH) * scroll_ / maxScroll;
        c.Fill({trackX, thumbY, 3, thumbH}, kAccent, 0.8);
    }

    // Detail strip --------------------------------------------------------
    const double dy = y + h - kDetailH - kFooterH;
    c.Fill({x + 1, dy, w - 2, kDetailH - 1}, kDetail, 0.85);
    c.Fill({x + 1, dy, w - 2, 1}, kBorder, 0.6);

    bool found = false;
    for (const auto& r : rows) {
        if (r.skillId != expanded_) continue;
        found = true;
        std::ostringstream line1;
        line1 << names_.Get(r.skillId) << "  -  " << r.hits << " hits, avg "
              << skillmeter::Compact(r.avgHit) << ", max "
              << skillmeter::Compact(r.maxHit) << ", crit "
              << skillmeter::Percent(r.critRate, 1) << ", "
              << skillmeter::Compact(r.dps) << " dps";
        c.Text(x + 8, dy + 4, line1.str(), kText, 0.85, 0, w - 16);

        const auto targets = meter_.TargetBreakdown(cfg_.View(), r.skillId);
        std::string line2 = "Targets: none recorded";
        if (!targets.empty()) {
            line2 = "Targets: ";
            for (std::size_t i = 0; i < targets.size() && i < 4; ++i) {
                if (i) line2 += "  |  ";
                line2 += targets[i].first + " " +
                         skillmeter::Compact(targets[i].second);
            }
        }
        c.Text(x + 8, dy + 19, line2, kMuted, 0.82, 0, w - 16);
        break;
    }
    if (!found)
        c.Text(x + w / 2, dy + 11,
               "Click a row for details  -  drag the title bar to move  -  wheel to scroll",
               kMuted, 0.82, 1, w - 16);

    // Footer --------------------------------------------------------------
    const double fy = y + h - kFooterH;
    c.Fill({x + 1, fy, w - 2, kFooterH - 1}, kTitle, 0.9);
    c.Text(x + 8, fy + 5,
           "F5 hide  F6 reset  F7 view  F8 sort  F9 export   |   "
           "fight " + std::to_string(sum.fightCount),
           kMuted, 0.8, 0, w - 16);
}
