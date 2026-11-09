#pragma once
// -----------------------------------------------------------------------------
// Overlay.hpp - in-game, Skada-style damage window.
// -----------------------------------------------------------------------------
#include "Config.hpp"
#include "Gml.hpp"
#include "Meter.hpp"
#include "SkillNames.hpp"

#include <cstdint>
#include <string>
#include <vector>

class Overlay {
public:
    Overlay(Config& cfg, skillmeter::Meter& meter, SkillNames& names);

    // Called once per frame from the GUIdraw hook. Always runs the accounting
    // tick, even when the window is hidden.
    void Frame(std::uint64_t nowMs);

    void Toggle();
    void ResetStats();
    void Export(std::uint64_t nowMs, bool quiet = false);
    bool Visible() const { return cfg_.visible; }

private:
    void HandleInput(std::uint64_t nowMs, const gml::Canvas& c);
    void Draw(std::uint64_t nowMs, const gml::Canvas& c);

    Config& cfg_;
    skillmeter::Meter& meter_;
    SkillNames& names_;

    double mx_ = 0.0, my_ = 0.0;      // mouse in virtual 960x540 space
    double dragDx_ = 0.0, dragDy_ = 0.0;
    bool dragging_ = false;
    int scroll_ = 0;
    int expanded_ = 0x7FFFFFFF;        // sentinel = nothing expanded
    std::uint64_t toastUntil_ = 0;
    std::size_t lastArchived_ = 0;
};
