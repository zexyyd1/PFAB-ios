#pragma once

#include "xs.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>

namespace cfg {
    inline int g_stage = -1;

    inline int stage() {
        if (g_stage < 0) g_stage = std::clamp((int)geode::Mod::get()->getSavedValue<int64_t>(XS("s0"), 0), 0, 6);
        return g_stage;
    }

    inline void setStage(int s) {
        g_stage = std::clamp(s, 0, 6);
        geode::Mod::get()->setSavedValue<int64_t>(XS("s0"), (int64_t)g_stage);
    }
}
