#pragma once

#include "sim.hpp"

#include <vector>

namespace learn {
    constexpr float kFailCost = 4.f;
    constexpr float kMaxFailCost = 40.f;
    constexpr float kPhantomCost = 25.f;

    uint64_t keyOf(Ghost const& g);
    float costOf(uint64_t key);
    void markFailed(uint64_t key);

    void addPhantom(CCRect const& r);
    std::vector<CCRect> const& phantoms();

    void enterLevel(PlayLayer* pl);
    void forget();
    size_t failedCount();
}
