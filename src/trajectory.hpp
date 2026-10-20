#pragma once

#include "sim.hpp"

#include <deque>
#include <vector>

namespace traj {
    constexpr int kLook = 480;
    constexpr int kBranchLook = 240;
    constexpr int kMaxBranches = 3;
    constexpr int kOtherEvery = 4;

    struct Branch {
        int at = 0;
        std::vector<CCPoint> points;
        int diesAt = -1;
        bool unknown = false;
    };

    struct Path {
        std::deque<CCPoint> points;
        int diesAt = -1;
        bool unknown = false;
        bool haveKiller = false;
        CCRect killer;
        bool ended = false;
        Ghost end;
        int age = 0;
        bool touching = false;
        std::deque<Branch> orbs;

        int survives() const { return diesAt >= 0 ? diesAt : unknown ? kLook : (int)points.size(); }
    };

    extern bool show;

    void update(PlayLayer* pl);
    void endTick();
    bool ready();
    Path const& path(bool hold);
    int survivesNow(bool hold, int ticks);
    void reset();
    void draw(PlayLayer* pl);
}
