#include "trajectory.hpp"

#include "search.hpp"

namespace traj {
    bool show = false;
}

namespace {
    traj::Path g_paths[2];
    bool g_ready = false;
    bool g_have = false;
    PlayLayer* g_layer = nullptr;
    PlayerObject* g_real = nullptr;
    Ghost g_scratch;
    Ref<CCDrawNode> g_draw;
    unsigned g_drawnFrame = 0;
    bool g_lastKept = false;

    constexpr int kShortLook = 160;
    int g_look = traj::kLook;
    int g_misses = 0;
    int g_hits = 0;

    constexpr size_t kGhostRelease = kGhostCount + 1;
    constexpr size_t kGhostHold = kGhostCount + 2;
    constexpr size_t kGhostScratch = kGhostCount + 3;

    void branchAt(traj::Path& path) {
        traj::Branch b;
        b.at = (int)path.points.size();
        copyGhost(g_scratch, path.end);
        for (int t = 0; t < traj::kBranchLook; t++) {
            auto st = simStep(g_scratch, t == 0);
            if (st == StepResult::Died) {
                b.diesAt = t;
                b.points.push_back(g_scratch.p->getPosition());
                break;
            }
            if (st == StepResult::Tainted) {
                b.unknown = true;
                break;
            }
            b.points.push_back(g_scratch.p->getPosition());
        }
        path.orbs.push_back(std::move(b));
    }

    void extend(traj::Path& path, bool hold) {
        if (path.ended) return;
        auto st = simStep(path.end, hold);
        if (st == StepResult::Died) {
            path.diesAt = (int)path.points.size();
            path.points.push_back(path.end.p->getPosition());
            if (auto k = sim::killer) {
                path.killer = k->getObjectRect();
                path.haveKiller = true;
            }
            path.ended = true;
            return;
        }
        if (st == StepResult::Tainted) {
            path.unknown = true;
            path.ended = true;
            return;
        }
        path.points.push_back(path.end.p->getPosition());
        bool touching = path.end.p->m_touchingRings && path.end.p->m_touchingRings->count() > 0;
        if (touching && !path.touching && !hold && (int)path.orbs.size() < traj::kMaxBranches) branchAt(path);
        path.touching = touching;
    }

    void recompute(traj::Path& path, PlayerObject* real, bool hold) {
        ghostFromReal(path.end, real);
        path.points.clear();
        path.orbs.clear();
        path.diesAt = -1;
        path.unknown = false;
        path.haveKiller = false;
        path.ended = false;
        path.age = 0;
        path.touching = real->m_touchingRings && real->m_touchingRings->count() > 0;
        for (int t = 0; t < g_look && !path.ended; t++) extend(path, hold);
    }

    void advance(traj::Path& path, bool hold) {
        path.points.pop_front();
        if (path.diesAt >= 0) path.diesAt--;
        for (auto& b : path.orbs) b.at--;
        while (!path.orbs.empty() && path.orbs.front().at < 0) path.orbs.pop_front();
        extend(path, hold);
        for (int extra = 0; extra < 8 && !path.ended && (int)path.points.size() < g_look; extra++) extend(path, hold);
    }
}

void traj::update(PlayLayer* pl) {
    auto real = pl ? pl->m_player1 : nullptr;
    if (!real || real->m_isDead || pl->m_gameState.m_isDualMode || pl->m_isPlatformer) {
        g_ready = false;
        g_have = false;
        return;
    }
    if (g_layer != pl) {
        g_layer = pl;
        g_have = false;
        g_paths[0].end.p = getGhost(pl, kGhostRelease);
        g_paths[1].end.p = getGhost(pl, kGhostHold);
        g_scratch.p = getGhost(pl, kGhostScratch);
    }
    if (!g_paths[0].end.p || !g_paths[1].end.p || !g_scratch.p) return;
    g_real = real;

    bool kept = isHeld(real);
    auto& follow = g_paths[kept];
    auto& other = g_paths[!kept];
    bool switched = g_have && kept != g_lastKept;
    g_lastKept = kept;

    simBegin(pl);
    bool matched = g_have && follow.age == 0 && !follow.points.empty() && follow.points.front() == real->getPosition();
    if (matched) {
        advance(follow, kept);
        if (g_misses > 0 && ++g_hits >= 60) {
            g_misses = 0;
            g_look = kLook;
        }
    } else {
        if (g_have && ++g_misses >= 3) g_look = kShortLook;
        g_hits = 0;
        recompute(follow, real, kept);
    }
    if (!g_have || switched || !matched || ++other.age >= kOtherEvery) recompute(other, real, !kept);
    simEnd();
    g_ready = true;
    g_have = true;
}

void traj::endTick() {
    g_ready = false;
}

bool traj::ready() {
    return g_ready;
}

traj::Path const& traj::path(bool hold) {
    return g_paths[hold];
}

int traj::survivesNow(bool hold, int ticks) {
    auto& p = g_paths[hold];
    if (g_ready && p.age == 0 && (p.diesAt >= 0 || p.unknown || (int)p.points.size() >= ticks)) {
        return std::min(p.survives(), ticks);
    }
    if (!g_ready || !g_layer || !g_real || !g_scratch.p) return ticks;
    simBegin(g_layer);
    ghostFromReal(g_scratch, g_real);
    int alive = ticks;
    for (int t = 0; t < ticks; t++) {
        auto st = simStep(g_scratch, hold);
        if (st == StepResult::Died) {
            alive = t;
            break;
        }
        if (st == StepResult::Tainted) break;
    }
    simEnd();
    return alive;
}

void traj::reset() {
    g_ready = false;
    g_have = false;
    g_look = kLook;
    g_misses = g_hits = 0;
    g_layer = nullptr;
    g_real = nullptr;
    g_draw = nullptr;
    g_scratch = {};
    for (auto& p : g_paths) {
        p.points.clear();
        p.orbs.clear();
        p.end = {};
    }
}

void traj::draw(PlayLayer* pl) {
    unsigned frame = CCDirector::sharedDirector()->getTotalFrames();
    if (frame == g_drawnFrame) return;
    g_drawnFrame = frame;
    if (!show || !g_have) {
        if (g_draw) g_draw->clear();
        return;
    }
    auto layer = pl->m_objectLayer;
    if (!layer) return;
    if (!g_draw || g_draw->getParent() != layer) {
        if (g_draw) g_draw->removeFromParent();
        g_draw = CCDrawNode::create();
        layer->addChild(g_draw, 9998);
    }
    g_draw->clear();
    auto real = pl->m_player1;
    if (!real) return;
    bool kept = isHeld(real);
    auto death = ccc4f(1.f, 0.15f, 0.15f, 1.f);

    auto polyline = [&](CCPoint from, auto const& points, size_t step, float width, ccColor4F color) {
        for (size_t i = 0; i < points.size(); i += step) {
            g_draw->drawSegment(from, points[i], width, color);
            from = points[i];
        }
        if (!points.empty()) g_draw->drawSegment(from, points.back(), width, color);
    };

    for (int hold = 0; hold < 2; hold++) {
        auto& p = g_paths[hold];
        bool current = (hold != 0) == kept;
        float a = current ? 0.95f : 0.45f;
        auto color = hold ? ccc4f(1.f, 0.55f, 0.1f, a) : ccc4f(0.2f, 1.f, 0.4f, a);
        polyline(real->getPosition(), p.points, 3, current ? 1.6f : 1.f, color);
        if (p.diesAt >= 0 && !p.points.empty()) {
            g_draw->drawDot(p.points.back(), 3.5f, death);
            if (p.haveKiller) {
                auto& r = p.killer;
                CCPoint v[4] = { r.origin, ccp(r.origin.x + r.size.width, r.origin.y),
                    ccp(r.origin.x + r.size.width, r.origin.y + r.size.height), ccp(r.origin.x, r.origin.y + r.size.height) };
                g_draw->drawPolygon(v, 4, ccc4f(1.f, 0.f, 0.f, 0.f), 1.f, ccc4f(1.f, 0.15f, 0.15f, 0.9f));
            }
        }
        for (auto& b : p.orbs) {
            if (b.at < 0 || b.at >= (int)p.points.size()) continue;
            auto start = b.at > 0 ? p.points[b.at - 1] : real->getPosition();
            g_draw->drawDot(start, 3.f, ccc4f(0.3f, 0.85f, 1.f, a));
            polyline(start, b.points, 3, 1.f, ccc4f(0.3f, 0.85f, 1.f, a * 0.85f));
            if (b.diesAt >= 0 && !b.points.empty()) g_draw->drawDot(b.points.back(), 3.f, death);
        }
    }
}
