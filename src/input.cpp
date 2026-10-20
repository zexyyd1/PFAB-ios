#include "input.hpp"

#include "bot.hpp"
#include "buffer.hpp"
#include "settings.hpp"
#include "sim.hpp"
#include "trajectory.hpp"

#include <Geode/modify/GJBaseGameLayer.hpp>

#include <algorithm>
#include <array>
#include <vector>

namespace {
    struct PendingInput {
        unsigned due;
        bool down;
        bool decided;
    };
    std::vector<PendingInput> g_pending;
    int g_activeDelay = 0;
    unsigned g_pressDue = 0;
    bool g_reentry = false;
    bool g_userHolding = false;
    bool g_autoReleased = false;

    std::array<unsigned, 9> g_inputTimes{};
    size_t g_inputCount = 0;

    void noteInput(unsigned now) {
        g_inputTimes[g_inputCount % g_inputTimes.size()] = now;
        g_inputCount++;
    }

    int usualHoldTicks() {
        size_t n = std::min(g_inputCount, g_inputTimes.size());
        if (n < 3) return 12;
        std::vector<unsigned> times;
        for (size_t i = 0; i < n; i++) times.push_back(g_inputTimes[(g_inputCount - n + i) % g_inputTimes.size()]);
        std::vector<int> gaps;
        for (size_t i = 1; i < times.size(); i++) gaps.push_back((int)(times[i] - times[i - 1]) / 2);
        std::nth_element(gaps.begin(), gaps.begin() + gaps.size() / 2, gaps.end());
        return std::clamp(gaps[gaps.size() / 2], 6, 30);
    }

    int stage() {
        return cfg::stage();
    }

    void flushPending(GJBaseGameLayer* gl) {
        auto inputs = std::move(g_pending);
        g_pending.clear();
        for (auto& in : inputs) input::apply(gl, in.down);
    }
}

void input::clearPending() {
    g_pending.clear();
    g_activeDelay = 0;
    g_pressDue = 0;
    g_inputCount = 0;
    g_autoReleased = false;
}

void input::apply(GJBaseGameLayer* gl, bool down) {
    g_reentry = true;
    gl->handleButton(down, 1, true);
    g_reentry = false;
}

class $modify(Rq1, GJBaseGameLayer) {
    void autoRelease() {
        if (!g_userHolding || g_autoReleased || !g_pending.empty() || stage() == 0) return;
        auto pl = PlayLayer::get();
        if (!pl || static_cast<GJBaseGameLayer*>(pl) != this || bot::ownsJump(this)) return;
        if (m_gameState.m_isDualMode || m_isPlatformer || !m_player1 || m_player1->m_isDead || !m_player1->m_isDart) return;
        if (!isHeld(m_player1) || !buffer::shouldAutoRelease(pl, usualHoldTicks())) return;
        input::apply(this, false);
        g_autoReleased = true;
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        if (g_reentry) {
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
            return;
        }
        auto pl = PlayLayer::get();
        bool ours = pl && static_cast<GJBaseGameLayer*>(pl) == this && button == 1 && isPlayer1;
        if (ours && bot::ownsJump(this)) return;

        if (ours) {
            g_userHolding = down;
            if (!down && g_autoReleased) {
                g_autoReleased = false;
                noteInput(m_gameState.m_currentProgress);
                return;
            }
            g_autoReleased = false;
        }

        int st = stage();
        if (!ours || st == 0 || m_gameState.m_isDualMode || m_isPlatformer || !m_player1 || m_player1->m_isDead) {
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
            return;
        }

        unsigned now = m_gameState.m_currentProgress;
        noteInput(now);
        if (down) {
            if (!g_pending.empty()) flushPending(this);
            int d = buffer::decideDelay(pl, st, true, usualHoldTicks());
            g_activeDelay = d;
            if (d == 0) {
                GJBaseGameLayer::handleButton(true, button, isPlayer1);
                return;
            }
            g_pressDue = now + 2u * (unsigned)d;
            g_pending.push_back({ g_pressDue, true, true });
            return;
        }

        bool pressPending = std::any_of(g_pending.begin(), g_pending.end(), [](PendingInput const& p) { return p.down; });
        if (pressPending) {
            unsigned due = std::max(now + 2u * (unsigned)g_activeDelay, g_pressDue + 2u);
            g_pending.push_back({ due, false, false });
            return;
        }
        g_activeDelay = 0;
        int d = buffer::decideDelay(pl, st, false, usualHoldTicks());
        if (d == 0) {
            GJBaseGameLayer::handleButton(false, button, isPlayer1);
            return;
        }
        g_pending.push_back({ now + 2u * (unsigned)d, false, true });
    }

    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        sim::halfTick = isHalfTick;
        auto pl = PlayLayer::get();
        bool mine = pl && static_cast<GJBaseGameLayer*>(pl) == this;
        if (mine && (traj::show || stage() > 0 || bot::on)) traj::update(pl);
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
        bot::tick(this);
        autoRelease();
        applyDue();
        traj::endTick();
        if (mine) traj::draw(pl);
    }

    void applyDue() {
        if (g_pending.empty()) return;
        unsigned now = m_gameState.m_currentProgress;
        while (!g_pending.empty() && g_pending.front().due <= now) {
            auto in = g_pending.front();
            g_pending.erase(g_pending.begin());
            if (!in.decided) {
                auto pl = PlayLayer::get();
                int d = pl && static_cast<GJBaseGameLayer*>(pl) == this ? buffer::decideDelay(pl, stage(), false, usualHoldTicks()) : 0;
                if (d > 0) {
                    PendingInput later{ now + 2u * (unsigned)d, false, true };
                    auto at = std::upper_bound(g_pending.begin(), g_pending.end(), later,
                        [](PendingInput const& a, PendingInput const& b) { return a.due < b.due; });
                    g_pending.insert(at, later);
                    continue;
                }
            }
            input::apply(this, in.down);
        }
    }
};
