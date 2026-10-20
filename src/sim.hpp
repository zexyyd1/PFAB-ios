#pragma once

#include "gd.hpp"

#include <vector>

struct Activation {
    EnhancedGameObject* obj;
    bool used;
};

struct BandView {
    bool active = false;
    float height = 0.f;
    float center = 0.f;
    bool freeMode = false;
    bool noSnap = false;
};

struct Ghost {
    PlayerObject* p = nullptr;
    bool held = false;
    std::vector<Activation> used;
    std::vector<int> touchPrev;
    std::vector<int> touchNow;
    BandView band;
    size_t nextSpeed = 0;
    float clear = 1e9f;
    float penalty = 0.f;
};

constexpr float kClearCap = 30.f;

enum class StepResult {
    Alive,
    Died,
    Tainted,
    Portal,
};

namespace sim {
    extern bool active;
    extern float dt;
    extern bool halfTick;
    extern GameObject* killer;
    extern bool scoring;
}

bool isHeld(PlayerObject* p);
PlayerObject* getGhost(PlayLayer* pl, size_t index);

void copyGhost(Ghost& dst, Ghost const& src);
void ghostFromReal(Ghost& dst, PlayerObject* real);

void simBegin(PlayLayer* pl);
void simEnd();
StepResult simStep(Ghost& g, bool hold);
