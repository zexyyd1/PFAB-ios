#include "learn.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace {
    PlayLayer* g_level = nullptr;
    std::unordered_map<uint64_t, int> g_failed;
    std::vector<CCRect> g_phantoms;
    constexpr size_t kMaxFailed = 200000;
    constexpr size_t kMaxPhantoms = 256;
}

uint64_t learn::keyOf(Ghost const& g) {
    auto p = g.p;
    auto x = (int64_t)std::floor(p->getPositionX() / 2.f);
    auto y = (int64_t)std::floor(p->getPositionY() / 2.f);
    auto v = (int64_t)std::floor(p->m_yVelocity / 0.5);
    uint64_t flags = (uint64_t)p->m_isShip | (uint64_t)p->m_isBird << 1 | (uint64_t)p->m_isDart << 2
        | (uint64_t)p->m_isBall << 3 | (uint64_t)p->m_isRobot << 4 | (uint64_t)p->m_isSpider << 5
        | (uint64_t)p->m_isSwing << 6 | (uint64_t)p->m_isUpsideDown << 7 | (uint64_t)g.held << 8
        | (uint64_t)p->m_isOnGround << 9 | (uint64_t)(p->m_vehicleSize < 1.f) << 10;
    uint64_t h = 1469598103934665603ull;
    for (uint64_t part : { (uint64_t)x, (uint64_t)y, (uint64_t)v, flags }) {
        h ^= part;
        h *= 1099511628211ull;
    }
    return h;
}

float learn::costOf(uint64_t key) {
    if (g_failed.empty()) return 0.f;
    auto it = g_failed.find(key);
    if (it == g_failed.end()) return 0.f;
    return std::min(kMaxFailCost, kFailCost * (float)it->second);
}

void learn::markFailed(uint64_t key) {
    if (g_failed.size() >= kMaxFailed && !g_failed.contains(key)) return;
    g_failed[key]++;
}

void learn::addPhantom(CCRect const& r) {
    for (auto& p : g_phantoms) {
        if (p.equals(r)) return;
    }
    if (g_phantoms.size() >= kMaxPhantoms) g_phantoms.erase(g_phantoms.begin());
    g_phantoms.push_back(r);
}

std::vector<CCRect> const& learn::phantoms() {
    return g_phantoms;
}

void learn::enterLevel(PlayLayer* pl) {
    if (g_level == pl) return;
    g_level = pl;
    forget();
}

void learn::forget() {
    g_failed.clear();
    g_phantoms.clear();
}

size_t learn::failedCount() {
    return g_failed.size();
}
