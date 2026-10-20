#include "buffer.hpp"

#include "sim.hpp"
#include "trajectory.hpp"

#include <algorithm>
#include <cfloat>

namespace {
    float progressOf(PlayerObject* p, float dir) {
        return dir * p->getPositionX();
    }

    bool steering(PlayerObject* p) {
        return p->m_isShip || p->m_isDart;
    }

    template <class Rest>
    int scan(int window, PlayerObject* real, Ghost& walk, Ghost& g, bool keepHold, Rest&& rest, int margin) {
        auto start = Clock::now();
        auto cap = std::chrono::microseconds(window <= 16 ? 12000 : window * 600);
        ghostFromReal(walk, real);
        copyGhost(g, walk);
        if (rest(g, 0)) return 0;
        int first = -1, last = -1;
        for (int d = 1; d <= window; d++) {
            if (simStep(walk, keepHold) != StepResult::Alive) break;
            copyGhost(g, walk);
            if (rest(g, d)) {
                if (first < 0) first = d;
                last = d;
            } else if (first >= 0) {
                break;
            }
            if (Clock::now() - start > cap) break;
        }
        if (first < 0) return 0;
        return first + std::min(margin, (last - first) / 2);
    }

    struct Roll {
        int diedAt = -1;
        bool unknown = false;
        float deathProgress = 0.f;
    };

    Roll rollNoInput(PlayerObject* real, Ghost& g, int cap, float dir) {
        ghostFromReal(g, real);
        Roll r;
        for (int t = 0; t < cap; t++) {
            auto st = simStep(g, false);
            if (st == StepResult::Died) {
                r.diedAt = t;
                r.deathProgress = progressOf(g.p, dir);
                return r;
            }
            if (st == StepResult::Tainted || st == StepResult::Portal) {
                r.unknown = true;
                return r;
            }
        }
        return r;
    }

    bool tapRest(Ghost& g, int at, float dir, float need, int passHold, int cap) {
        bool airborne = false, passed = false;
        int grounded = 0, sincePass = 0;
        for (int t = at; t < cap; t++) {
            auto st = simStep(g, t == at);
            if (st == StepResult::Died) return false;
            if (st == StepResult::Tainted || st == StepResult::Portal) return true;
            if (!g.p->m_isOnGround) airborne = true;
            if (!passed && progressOf(g.p, dir) >= need) passed = true;
            if (!passed) continue;
            sincePass++;
            grounded = (airborne && g.p->m_isOnGround) ? grounded + 1 : 0;
            if (grounded >= 4 || sincePass >= passHold) return true;
        }
        return passed;
    }

    int decidePress(PlayerObject* real, Ghost& walk, Ghost& g, int window) {
        float dir = real->m_isGoingLeft ? -1.f : 1.f;
        auto base = rollNoInput(real, g, window + 150, dir);
        if (base.unknown) return 0;
        bool threat = base.diedAt >= 0;
        float need = threat ? base.deathProgress + 45.f : -FLT_MAX;
        int passHold = threat ? 40 : 150;
        int cap = (threat ? base.diedAt : 150) + window + 80;
        return scan(window, real, walk, g, false,
            [&](Ghost& from, int d) { return tapRest(from, d, dir, need, passHold, cap); }, 2);
    }

    bool releaseRest(Ghost& g, int at, int cap) {
        bool airborne = false;
        int grounded = 0;
        for (int t = at; t < cap; t++) {
            auto st = simStep(g, false);
            if (st == StepResult::Died) return false;
            if (st == StepResult::Tainted || st == StepResult::Portal) return true;
            if (!g.p->m_isOnGround) airborne = true;
            grounded = (airborne && g.p->m_isOnGround) ? grounded + 1 : 0;
            if (grounded >= 4) return true;
        }
        return true;
    }

    int decideRelease(PlayerObject* real, Ghost& walk, Ghost& g, int window) {
        if (!isHeld(real)) return 0;
        return scan(window, real, walk, g, true,
            [&](Ghost& from, int d) { return releaseRest(from, d, d + 120); }, 2);
    }

    bool switchRest(Ghost& g, bool target, int hold) {
        for (int t = 0; t < hold; t++) {
            auto st = simStep(g, target);
            if (st == StepResult::Died) return false;
            if (st == StepResult::Tainted || st == StepResult::Portal) return true;
        }
        return true;
    }

    bool switchWorks(PlayerObject* real, Ghost& g, int at, bool target, int hold) {
        ghostFromReal(g, real);
        for (int t = 0; t < at; t++) {
            auto st = simStep(g, !target);
            if (st == StepResult::Died) return false;
            if (st == StepResult::Tainted || st == StepResult::Portal) return true;
        }
        return switchRest(g, target, hold);
    }

    int decideSwitch(PlayerObject* real, Ghost& walk, Ghost& g, int window, bool down, int hold) {
        if (isHeld(real) == down) return 0;
        return scan(window, real, walk, g, !down,
            [&](Ghost& from, int) { return switchRest(from, down, hold); }, 1);
    }
}

bool buffer::shouldAutoRelease(PlayLayer* pl, int holdTicks) {
    auto real = pl->m_player1;
    if (!real || !real->m_isDart || !isHeld(real)) return false;
    constexpr int kMargin = 1;

    if (traj::ready()) {
        if (traj::survivesNow(true, holdTicks + kMargin + 2) > holdTicks + kMargin + 1) return false;
        if (traj::survivesNow(false, holdTicks) < holdTicks) return false;
    }

    Ghost g{ getGhost(pl, 0) };
    if (!g.p) return false;
    simBegin(pl);
    bool holdingDies = traj::ready() || !switchWorks(real, g, holdTicks + kMargin + 1, false, 0);
    bool laterTooLate = holdingDies && !switchWorks(real, g, kMargin + 1, false, holdTicks);
    bool nowWorks = laterTooLate && (traj::ready() || switchWorks(real, g, 0, false, holdTicks));
    simEnd();
    return nowWorks;
}

int buffer::decideDelay(PlayLayer* pl, int stage, bool down, int holdTicks) {
    auto real = pl->m_player1;
    if (!real) return 0;

    if (traj::ready()) {
        if (steering(real) && traj::survivesNow(down, holdTicks) >= holdTicks) return 0;
        if (!steering(real) && !down && traj::survivesNow(false, 120) >= 120) return 0;
    }
    Ghost g{ getGhost(pl, 0) };
    Ghost walk{ getGhost(pl, 1) };
    if (!g.p || !walk.p) return 0;

    int window = 1 << stage;
    simBegin(pl);
    int result;
    if (steering(real)) result = decideSwitch(real, walk, g, window, down, holdTicks);
    else if (down) result = decidePress(real, walk, g, window);
    else result = decideRelease(real, walk, g, window);
    simEnd();
    return result;
}
