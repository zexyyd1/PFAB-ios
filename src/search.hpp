#pragma once

#include "sim.hpp"

#include <coroutine>
#include <memory>
#include <utility>
#include <vector>

struct PlanTask {
    struct promise_type {
        bool value = false;
        std::coroutine_handle<> next;

        PlanTask get_return_object() { return PlanTask(std::coroutine_handle<promise_type>::from_promise(*this)); }
        std::suspend_always initial_suspend() noexcept { return {}; }
        struct Final {
            bool await_ready() noexcept { return false; }
            std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> h) noexcept {
                auto n = h.promise().next;
                return n ? n : std::noop_coroutine();
            }
            void await_resume() noexcept {}
        };
        Final final_suspend() noexcept { return {}; }
        void return_value(bool v) { value = v; }
        void unhandled_exception() { std::terminate(); }
    };
    using Handle = std::coroutine_handle<promise_type>;

    Handle h{};

    PlanTask() = default;
    explicit PlanTask(Handle h) : h(h) {}
    PlanTask(PlanTask&& o) noexcept : h(std::exchange(o.h, {})) {}
    PlanTask& operator=(PlanTask&& o) noexcept {
        if (this != &o) {
            if (h) h.destroy();
            h = std::exchange(o.h, {});
        }
        return *this;
    }
    PlanTask(PlanTask const&) = delete;
    PlanTask& operator=(PlanTask const&) = delete;
    ~PlanTask() {
        if (h) h.destroy();
    }

    bool await_ready() const noexcept { return false; }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> c) noexcept {
        h.promise().next = c;
        return h;
    }
    bool await_resume() const noexcept { return h.promise().value; }
};

enum class PlanMode { Click, Robot, Tap, Ship, Wave };

PlanMode modeOf(PlayerObject* p);

struct Search {
    struct Node {
        Ghost g;
        std::vector<uint8_t> path;
        float cost = 0.f;
        int heldFor = 0;
        float y = 0.f;
        float score() const;
    };
    struct Pause {
        Search* s;
        bool await_ready() const noexcept { return Clock::now() < s->sliceEnd; }
        void await_suspend(std::coroutine_handle<> h) noexcept { s->resumeAt = h; }
        void await_resume() const noexcept {}
    };

    PlayLayer* pl = nullptr;
    Ghost root;
    int H = 150;
    int width = 32;
    long budget = 150000;
    long steps = 0;

    bool done = false;
    bool ok = false;
    std::vector<uint8_t> out;
    int endAt = -1;
    bool endBlocked = false;
    int commitLen = 0;

    std::vector<PlayerObject*> spare;

    Clock::time_point sliceEnd;
    std::coroutine_handle<> resumeAt;
    PlanTask top;

    bool run(Clock::duration slice);

    Pause pause() { return { this }; }
    bool over() const { return steps >= budget; }

    PlanTask start();
    PlayerObject* takeGhost();
    void giveGhost(PlayerObject* p);
    uint64_t keyOf(Node const& n) const;
};

std::unique_ptr<Search> makeSearch(PlayLayer* pl, Ghost const* from, PlayerObject* real, bool urgent, int horizon);

constexpr size_t kSearchWidth = 40;
constexpr size_t kGhostCount = 3 + 1 + 2 * kSearchWidth + 2;
