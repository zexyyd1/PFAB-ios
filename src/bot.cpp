#include "bot.hpp"

#include "input.hpp"
#include "learn.hpp"
#include "search.hpp"
#include "sim.hpp"
#include "trajectory.hpp"

#include <Geode/modify/GJGameLevel.hpp>

#include <algorithm>
#include <deque>
#include <memory>

namespace bot {
    bool on = false;
    bool showPath = false;
}

namespace {
    constexpr int kLookahead = 240;
    constexpr int kHorizon = 240;
    constexpr int kRecent = 90;

    bool g_wasOn = false;
    PlayLayer* g_layer = nullptr;
    std::deque<uint8_t> g_plan;
    std::deque<CCPoint> g_predicted;
    std::deque<uint64_t> g_planKeys;
    std::deque<uint64_t> g_recent;
    Ghost g_tail;
    Ghost g_val;
    bool g_tailValid = false;
    bool g_tailBlocked = false;
    bool g_haveLast = false;
    CCPoint g_last;
    int g_cooldown = 0;
    int g_failStreak = 0;
    std::unique_ptr<Search> g_search;
    Ref<CCDrawNode> g_draw;

    unsigned g_frame = 0;
    double g_frameMs = 0;

    bool g_usedThisAttempt = false;
    bool g_forcedTestMode = false;
    bool g_savedTestMode = false;
    bool g_deathSeen = false;

    void clearPlan() {
        g_plan.clear();
        g_predicted.clear();
        g_planKeys.clear();
    }

    void resetPlan() {
        g_search.reset();
        clearPlan();
        g_tailValid = false;
        g_tailBlocked = false;
        g_haveLast = false;
        g_cooldown = 0;
    }

    void truncatePlan(size_t keep) {
        if (g_plan.size() <= keep) return;
        g_plan.resize(keep);
        g_predicted.resize(keep);
        g_planKeys.resize(keep);
    }

    bool canRun(GJBaseGameLayer* gl, PlayLayer* pl) {
        if (!pl || static_cast<GJBaseGameLayer*>(pl) != gl) return false;
        auto real = gl->m_player1;
        return real && !real->m_isDead && !gl->m_gameState.m_isDualMode && !gl->m_isPlatformer && !pl->m_hasCompletedLevel;
    }

    bool commit(Search& s) {
        int n = std::min(s.commitLen, (int)s.out.size());
        size_t before = g_plan.size();
        simBegin(g_layer);
        copyGhost(g_val, s.root);
        bool ok = true;
        for (int i = 0; i < n; i++) {
            auto st = simStep(g_val, s.out[i] != 0);
            if (st == StepResult::Died || st == StepResult::Tainted) {
                ok = false;
                break;
            }
            g_plan.push_back(s.out[i]);
            g_predicted.push_back(g_val.p->getPosition());
            g_planKeys.push_back(learn::keyOf(g_val));
        }
        if (ok) {
            copyGhost(g_tail, g_val);
            g_tailValid = true;
            g_tailBlocked = s.endBlocked && s.endAt >= 0 && n >= s.endAt;
        } else {
            truncatePlan(before);
        }
        simEnd();
        return ok;
    }

    void backtrack(PlayerObject* real, size_t keep) {
        truncatePlan(keep);
        simBegin(g_layer);
        ghostFromReal(g_val, real);
        bool ok = true;
        for (auto bit : g_plan) {
            auto st = simStep(g_val, bit != 0);
            if (st == StepResult::Died || st == StepResult::Tainted) {
                ok = false;
                break;
            }
        }
        if (ok) {
            copyGhost(g_tail, g_val);
            g_tailValid = true;
            g_tailBlocked = false;
        } else {
            clearPlan();
            g_tailValid = false;
        }
        simEnd();
    }

    void drawPath(PlayLayer* pl, PlayerObject* real) {
        if (!bot::showPath) {
            if (g_draw) g_draw->clear();
            return;
        }
        auto layer = pl->m_objectLayer;
        if (!layer) return;
        if (!g_draw || g_draw->getParent() != layer) {
            if (g_draw) g_draw->removeFromParent();
            g_draw = CCDrawNode::create();
            layer->addChild(g_draw, 9999);
        }
        g_draw->clear();
        auto prev = real->getPosition();
        for (size_t i = 0; i < g_predicted.size(); i += 2) {
            auto color = g_plan[i] ? ccc4f(1.f, 0.55f, 0.1f, 0.9f) : ccc4f(0.2f, 1.f, 0.4f, 0.9f);
            g_draw->drawSegment(prev, g_predicted[i], 1.5f, color);
            prev = g_predicted[i];
        }
        if (g_tailValid) g_draw->drawDot(prev, 4.f, g_tailBlocked ? ccc4f(1.f, 0.2f, 0.2f, 1.f) : ccc4f(0.3f, 0.7f, 1.f, 1.f));
        for (auto& r : learn::phantoms()) {
            CCPoint v[4] = { r.origin, ccp(r.origin.x + r.size.width, r.origin.y),
                ccp(r.origin.x + r.size.width, r.origin.y + r.size.height), ccp(r.origin.x, r.origin.y + r.size.height) };
            g_draw->drawPolygon(v, 4, ccc4f(1.f, 0.f, 0.3f, 0.15f), 1.f, ccc4f(1.f, 0.f, 0.3f, 0.8f));
        }
    }

    double frameBudgetMs(int ahead) {
        double cap = ahead >= 60 ? 1.5 : ahead >= 30 ? 3.0 : 6.0;
        return std::max(0.0, cap - g_frameMs);
    }

    std::chrono::microseconds ms(double v) {
        return std::chrono::microseconds((long long)(v * 1000.0));
    }
}

bool bot::ownsJump(GJBaseGameLayer* gl) {
    return on && canRun(gl, PlayLayer::get());
}

bool bot::blocksSaving() {
    return g_usedThisAttempt;
}

void bot::onRealDeath(PlayerObject* player, GameObject* object) {
    if (!on || g_deathSeen || !g_layer || player != g_layer->m_player1 || player->m_isDead) return;
    g_deathSeen = true;
    bool predicted = g_plan.empty();
    int marked = 0;
    for (auto it = g_recent.rbegin(); it != g_recent.rend() && marked < 30; ++it, ++marked) learn::markFailed(*it);
    if (!predicted && object) learn::addPhantom(object->getObjectRect());
}

void bot::onAttemptEnd(GJBaseGameLayer* gl) {
    if (g_forcedTestMode) {
        gl->m_isTestMode = g_savedTestMode;
        g_forcedTestMode = false;
    }
    g_usedThisAttempt = false;
    g_deathSeen = false;
    g_failStreak = 0;
    g_recent.clear();
    resetPlan();
}

void bot::onLevelExit() {
    resetPlan();
    g_layer = nullptr;
    g_draw = nullptr;
    learn::enterLevel(nullptr);
}

void bot::forget() {
    learn::forget();
}

size_t bot::learnedSpots() {
    return learn::failedCount() + learn::phantoms().size();
}

void bot::tick(GJBaseGameLayer* gl) {
    auto pl = PlayLayer::get();
    if (!on) {
        if (g_wasOn && canRun(gl, pl) && isHeld(gl->m_player1)) input::apply(gl, false);
        if (g_wasOn) {
            resetPlan();
            if (g_draw) g_draw->clear();
        }
        g_wasOn = false;
        return;
    }
    if (!canRun(gl, pl)) {
        resetPlan();
        return;
    }
    auto start = Clock::now();
    auto real = gl->m_player1;

    unsigned frame = CCDirector::sharedDirector()->getTotalFrames();
    bool newFrame = frame != g_frame;
    if (newFrame) {
        g_frame = frame;
        g_frameMs = 0;
    }

    if (g_layer != pl || !g_wasOn) {
        resetPlan();
        g_layer = pl;
        learn::enterLevel(pl);
        getGhost(pl, kGhostCount);
        g_tail.p = getGhost(pl, 1);
        g_val.p = getGhost(pl, 2);
    }
    g_wasOn = true;
    if (!g_forcedTestMode) {
        g_savedTestMode = gl->m_isTestMode;
        gl->m_isTestMode = true;
        g_forcedTestMode = true;
    }
    g_usedThisAttempt = true;
    input::clearPending();

    if (g_haveLast) {
        auto a = real->getPosition();
        if (std::abs(a.x - g_last.x) + std::abs(a.y - g_last.y) > 0.05f) resetPlan();
    }

    int ahead = (int)g_plan.size();

    if (g_search) {
        auto& s = *g_search;
        double budget = frameBudgetMs(ahead);
        if (ahead == 0) budget = std::max(budget, 3.0);
        if (budget > 0.05 && s.run(ms(budget))) {
            if (s.ok) {
                g_failStreak = 0;
                if (!commit(s)) backtrack(real, std::min<size_t>(g_plan.size(), 8));
            } else {
                g_failStreak++;
                if (g_tailValid) learn::markFailed(learn::keyOf(g_tail));
                if (g_plan.size() > 8) backtrack(real, 8);
                else g_tailBlocked = true;
            }
            g_search.reset();
        } else if (ahead == 0) {
            g_search.reset();
        }
    }

    if (g_plan.empty() && !g_search) {
        g_tailValid = false;
        if (g_cooldown > 0) {
            g_cooldown--;
        } else if (frameBudgetMs(0) >= 1.0) {
            g_cooldown = 3;
            auto s = makeSearch(pl, nullptr, real, true, kHorizon);
            if (s && s->run(ms(std::min(frameBudgetMs(0), 4.0))) && s->ok && commit(*s)) g_cooldown = 0;
        }
    }

    if (!g_search && g_tailValid && !g_tailBlocked && !g_plan.empty() && (int)g_plan.size() < kLookahead) {
        int horizon = kHorizon + 90 * std::min(g_failStreak, 2);
        g_search = makeSearch(pl, &g_tail, nullptr, false, horizon);
    }

    bool want = isHeld(real);
    g_haveLast = false;
    if (g_plan.empty() && traj::ready()) {
        constexpr int kLastMoment = 10;
        int keepFor = traj::survivesNow(want, kLastMoment + 1);
        if (keepFor <= kLastMoment && traj::survivesNow(!want, 120) > keepFor) want = !want;
    }
    if (!g_plan.empty()) {
        want = g_plan.front() != 0;
        g_last = g_predicted.front();
        g_haveLast = true;
        g_recent.push_back(g_planKeys.front());
        if ((int)g_recent.size() > kRecent) g_recent.pop_front();
        g_plan.pop_front();
        g_predicted.pop_front();
        g_planKeys.pop_front();
    }
    if (!real->m_inputsLocked && want != isHeld(real)) input::apply(gl, want);

    if (newFrame) drawPath(pl, real);

    g_frameMs += std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

class $modify(Rq4, GJGameLevel) {
    void savePercentage(int percent, bool isPracticeMode, int clicks, int attempts, bool isChkValid) {
        if (bot::blocksSaving() && PlayLayer::get()) return;
        GJGameLevel::savePercentage(percent, isPracticeMode, clicks, attempts, isChkValid);
    }
};
