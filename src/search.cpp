#include "search.hpp"

#include "learn.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace {
    bool actable(PlayerObject* p) {
        return p->m_isOnGround || (p->m_touchingRings && p->m_touchingRings->count() > 0);
    }

    bool groundMode(PlanMode m) {
        return m == PlanMode::Click || m == PlanMode::Robot;
    }

    float clearCap(PlanMode m) {
        return groundMode(m) ? kClearCap : 15.f;
    }

    float switchCost(PlanMode m) {
        switch (m) {
            case PlanMode::Wave: return 1.0f;
            case PlanMode::Ship: return 0.6f;
            case PlanMode::Tap: return 0.3f;
            default: return 0.25f;
        }
    }
}

PlanMode modeOf(PlayerObject* p) {
    if (p->m_isSwing || p->m_isBird) return PlanMode::Tap;
    if (p->m_isDart) return PlanMode::Wave;
    if (p->m_isShip) return PlanMode::Ship;
    if (p->m_isRobot) return PlanMode::Robot;
    return PlanMode::Click;
}

float Search::Node::score() const {
    return std::min(g.clear, clearCap(modeOf(g.p))) - cost - g.penalty;
}

bool Search::run(Clock::duration slice) {
    if (done) return true;
    sliceEnd = Clock::now() + slice;
    simBegin(pl);
    sim::scoring = true;
    auto h = std::exchange(resumeAt, nullptr);
    if (h) h.resume();
    sim::scoring = false;
    simEnd();
    if (top.h.done()) {
        done = true;
        ok = top.h.promise().value;
    }
    return done;
}

PlayerObject* Search::takeGhost() {
    if (spare.empty()) return nullptr;
    auto p = spare.back();
    spare.pop_back();
    return p;
}

void Search::giveGhost(PlayerObject* p) {
    if (p) spare.push_back(p);
}

uint64_t Search::keyOf(Node const& n) const {
    auto p = n.g.p;
    bool fine = groundMode(modeOf(p));
    float yq = fine ? 0.5f : 1.5f;
    double vq = fine ? 0.1 : 0.4;
    auto y = (int64_t)std::floor(p->getPositionY() / yq);
    auto v = (int64_t)std::floor(p->m_yVelocity / vq);
    uint64_t k = (uint64_t)(y & 0xFFFFFF) | ((uint64_t)(v & 0xFFFF) << 24);
    k |= (uint64_t)n.g.held << 40;
    k |= (uint64_t)p->m_isUpsideDown << 41;
    k |= (uint64_t)p->m_isOnGround << 42;
    k |= (uint64_t)p->m_isDashing << 43;
    k |= (uint64_t)((int)modeOf(p) & 0x7) << 44;
    k |= (uint64_t)(n.g.used.size() & 0x1F) << 47;
    k |= (uint64_t)(std::min(n.heldFor, 63)) << 52;
    return k;
}

PlanTask Search::start() {
    out.assign(H, 0);

    std::vector<Node> frontier, next;
    std::unordered_map<uint64_t, size_t> index;
    Node first;
    first.g.p = takeGhost();
    if (!first.g.p) co_return false;
    copyGhost(first.g, root);
    first.g.clear = 1e9f;
    first.g.penalty = 0.f;
    frontier.push_back(std::move(first));

    bool haveEnded = false;
    float endedScore = -1e9f;
    std::vector<uint8_t> endedPath;

    int t = 0;
    for (; t < H && !frontier.empty(); t++) {
        next.clear();
        index.clear();
        for (auto& n : frontier) {
            auto m = modeOf(n.g.p);
            bool held = n.g.held;
            uint8_t acts[2];
            int nacts = 0;
            switch (m) {
                case PlanMode::Click:
                case PlanMode::Robot: {
                    acts[nacts++] = 0;
                    if (held) {
                        if (n.heldFor < 90 || n.g.p->m_isDashing) acts[nacts++] = 1;
                    } else if (actable(n.g.p)) {
                        acts[nacts++] = 1;
                    }
                    break;
                }
                case PlanMode::Tap:
                    acts[nacts++] = 0;
                    if (!held && (t & 1) == 0) acts[nacts++] = 1;
                    break;
                case PlanMode::Ship:
                    if ((t & 1) == 0) {
                        acts[nacts++] = 0;
                        acts[nacts++] = 1;
                    } else {
                        acts[nacts++] = held;
                    }
                    break;
                case PlanMode::Wave:
                    acts[nacts++] = 0;
                    acts[nacts++] = 1;
                    break;
            }

            for (int a = 0; a < nacts; a++) {
                if (over()) co_return false;
                Node c;
                if (a == nacts - 1) {
                    c = std::move(n);
                    n.g.p = nullptr;
                } else {
                    c.g.p = takeGhost();
                    if (!c.g.p) co_return false;
                    copyGhost(c.g, n.g);
                    c.path = n.path;
                    c.cost = n.cost;
                    c.heldFor = n.heldFor;
                }

                bool bit = acts[a] != 0;
                if (bit != c.g.held) c.cost += switchCost(m) + 0.0005f * (float)t;
                c.heldFor = bit ? c.heldFor + 1 : 0;
                c.path.push_back(bit);
                steps++;
                auto st = simStep(c.g, bit);
                if (st == StepResult::Alive || st == StepResult::Portal) {
                    c.g.penalty += learn::costOf(learn::keyOf(c.g));
                }
                if (st == StepResult::Died) {
                    giveGhost(c.g.p);
                    continue;
                }
                if (st == StepResult::Tainted) {
                    float s = c.score() - 5.f;
                    if (!haveEnded || s > endedScore) {
                        haveEnded = true;
                        endedScore = s;
                        endedPath = c.path;
                    }
                    giveGhost(c.g.p);
                    continue;
                }
                c.y = c.g.p->getPositionY();
                auto key = keyOf(c);
                if (auto it = index.find(key); it != index.end()) {
                    auto& other = next[it->second];
                    if (c.score() > other.score()) {
                        giveGhost(other.g.p);
                        other = std::move(c);
                    } else {
                        giveGhost(c.g.p);
                    }
                    continue;
                }
                index.emplace(key, next.size());
                next.push_back(std::move(c));
            }
            co_await pause();
        }
        frontier.clear();
        if (next.empty()) break;

        if ((int)next.size() > width) {
            std::sort(next.begin(), next.end(), [](Node const& a, Node const& b) { return a.score() > b.score(); });
            std::vector<uint8_t> taken(next.size(), 0);
            std::vector<Node> keep;
            keep.reserve(width);
            auto crowded = [&](Node const& n) {
                float sep = groundMode(modeOf(n.g.p)) ? 0.5f : 2.f;
                for (auto& k : keep) {
                    if (k.g.held == n.g.held && std::abs(k.y - n.y) < sep) return true;
                }
                return false;
            };
            for (size_t i = 0; i < next.size() && (int)keep.size() < width / 3; i++) {
                auto p = next[i].g.p;
                if (!groundMode(modeOf(p)) || !actable(p) || crowded(next[i])) continue;
                taken[i] = 1;
                keep.push_back(std::move(next[i]));
            }
            for (size_t i = 0; i < next.size() && (int)keep.size() < width; i++) {
                if (taken[i] || crowded(next[i])) continue;
                taken[i] = 1;
                keep.push_back(std::move(next[i]));
            }
            for (size_t i = 0; i < next.size() && (int)keep.size() < width; i++) {
                if (taken[i]) continue;
                taken[i] = 1;
                keep.push_back(std::move(next[i]));
            }
            for (size_t i = 0; i < next.size(); i++) {
                if (!taken[i]) giveGhost(next[i].g.p);
            }
            next = std::move(keep);
        }
        std::swap(frontier, next);
    }

    Node const* best = nullptr;
    float bestScore = -1e9f;
    if (t >= H) {
        for (auto& n : frontier) {
            float s = n.score();
            if (groundMode(modeOf(n.g.p)) && !actable(n.g.p)) s -= 4.f;
            if (!best || s > bestScore) {
                best = &n;
                bestScore = s;
            }
        }
    }
    bool found = false;
    if (best && (!haveEnded || bestScore >= endedScore)) {
        std::copy(best->path.begin(), best->path.begin() + std::min(H, (int)best->path.size()), out.begin());
        endAt = -1;
        endBlocked = false;
        found = true;
    } else if (haveEnded) {
        std::copy(endedPath.begin(), endedPath.begin() + std::min(H, (int)endedPath.size()), out.begin());
        endAt = (int)endedPath.size() - 1;
        endBlocked = true;
        found = true;
    }
    for (auto& n : frontier) giveGhost(n.g.p);
    if (found) {
        int len = std::min(100, H / 2);
        if (endAt >= 0) len = std::min(len, endAt);
        commitLen = std::max(len, 0);
    }
    co_return found;
}

std::unique_ptr<Search> makeSearch(PlayLayer* pl, Ghost const* from, PlayerObject* real, bool urgent, int horizon) {
    auto s = std::make_unique<Search>();
    s->pl = pl;
    s->H = horizon;
    s->width = (int)kSearchWidth;
    if (urgent) {
        s->H = 60;
        s->width = 12;
        s->budget = 4000;
    }

    size_t next = 3;
    s->root.p = getGhost(pl, next++);
    if (!s->root.p) return nullptr;
    for (int i = 0; i < 2 * s->width + 2; i++) {
        auto g = getGhost(pl, next++);
        if (!g) return nullptr;
        s->spare.push_back(g);
    }

    simBegin(pl);
    if (from) copyGhost(s->root, *from);
    else ghostFromReal(s->root, real);
    simEnd();

    s->top = s->start();
    s->resumeAt = s->top.h;
    return s;
}
