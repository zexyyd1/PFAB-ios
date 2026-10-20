#pragma once

#include <Geode/Geode.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>

using namespace geode::prelude;
using Clock = std::chrono::steady_clock;

namespace gdx {
    template <class T>
    inline T& at(void const* base, size_t offset) {
        return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(base) + offset);
    }

    constexpr size_t kDamageCheck = 12444;
    constexpr size_t kDamageSkip = 2182;
    constexpr size_t kBandCenter = 776;
    constexpr size_t kBandLastTop = 1892;

    constexpr size_t kMultiActivateArg = 2928;

    constexpr size_t kSizeScaleX = 916;
    constexpr size_t kSizeScaleY = 920;
    constexpr size_t kActivatedAny = 1459;
    constexpr size_t kPortalOwnCamera = 1256;
    constexpr size_t kPortalFreeMode = 1764;
    constexpr size_t kPortalNoSnap = 1776;
    constexpr size_t kRingNoAuto = 1856;

    inline float bandTop(GJBaseGameLayer* layer) {
        using Fn = float (*)(GJBaseGameLayer*);
        return reinterpret_cast<Fn>(geode::base::get() + 0x213770)(layer);
    }
}
