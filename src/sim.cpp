#include "sim.hpp"

#include "bot.hpp"
#include "input.hpp"
#include "learn.hpp"
#include "trajectory.hpp"

#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/GameObject.hpp>
#include <Geode/modify/HardStreak.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>

#include <algorithm>
#include <cmath>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"
static_assert(offsetof(PlayerObject, m_yVelocity) == 2464);
static_assert(offsetof(PlayerObject, m_isShip) == 2489);
static_assert(offsetof(PlayerObject, m_isSwing) == 2500);
static_assert(offsetof(PlayerObject, m_playerSpeed) == 2548);
static_assert(offsetof(PlayerObject, m_shipRotation) == 2552);
static_assert(offsetof(PlayerObject, m_lastPortalPos) == 2560);
static_assert(offsetof(PlayerObject, m_touchingRings) == 2616);
static_assert(offsetof(PlayerObject, m_lastActivatedPortal) == 2688);
static_assert(offsetof(PlayerObject, m_position) == 2704);
static_assert(offsetof(PlayerObject, m_totalTime) == 2720);
static_assert(offsetof(PlayerObject, m_holdingButtons) == 3000);
static_assert(offsetof(PlayerObject, m_inputsLocked) == 3016);
static_assert(offsetof(PlayerObject, m_gameLayer) == 3104);
static_assert(offsetof(GameObject, m_uniqueID) == 924);
static_assert(offsetof(GameObject, m_hasNoEffects) == 1052);
static_assert(offsetof(EnhancedGameObject, m_activatedByPlayer1) == 1460);
static_assert(offsetof(GJBaseGameLayer, m_gameState) == 424);
static_assert(offsetof(GJBaseGameLayer, m_isTestMode) == 12848);
static_assert(offsetof(GJBaseGameLayer, m_anticheatSpike) == 14216);
static_assert(offsetof(GJGameState, m_unkBool1) == 248);
static_assert(offsetof(GJGameState, m_lastModeHeight) == 324);
static_assert(offsetof(GJGameState, m_isFreeMode) == 361);
static_assert(offsetof(GJGameState, m_disableCameraGridSnap) == 362);
static_assert(offsetof(GJGameState, m_commandIndex) == 568);
static_assert(offsetof(GJGameState, m_isDualMode) == 634);
static_assert(offsetof(GJGameState, m_activatedObjectIDs) == 840);
#pragma clang diagnostic pop

namespace sim {
    bool active = false;
    float dt = 0.25f;
    bool halfTick = false;
    GameObject* killer = nullptr;
    bool scoring = false;
}

namespace {
    PlayLayer* g_layer = nullptr;
    Ghost* g_cur = nullptr;
    bool g_died = false;
    bool g_tainted = false;
    bool g_portal = false;
    bool g_damageCheck = false;
    bool g_damageSkip = false;

    struct SpeedPortal {
        float x;
        float speed;
    };
    std::vector<SpeedPortal> g_speedPortals;
    PlayLayer* g_speedLayer = nullptr;

    bool isGhost(PlayerObject* p) {
        return sim::active && g_cur && g_cur->p == p;
    }

    enum class Kind { Ignore, Keep, Stateful, ModePortal, Taint };

    Kind classify(GameObjectType t) {
        using T = GameObjectType;
        switch (t) {
            case T::Solid: case T::Hazard: case T::AnimatedHazard: case T::Slope: case T::Special:
                return Kind::Keep;
            case T::YellowJumpPad: case T::PinkJumpPad: case T::GravityPad: case T::RedJumpPad: case T::SpiderPad:
            case T::YellowJumpRing: case T::PinkJumpRing: case T::GravityRing: case T::GreenRing: case T::DropRing:
            case T::RedJumpRing: case T::DashRing: case T::GravityDashRing: case T::SpiderOrb:
            case T::InverseGravityPortal: case T::NormalGravityPortal: case T::GravityTogglePortal:
            case T::RegularSizePortal: case T::MiniSizePortal:
                return Kind::Stateful;
            case T::ShipPortal: case T::CubePortal: case T::BallPortal: case T::UfoPortal: case T::WavePortal:
            case T::RobotPortal: case T::SpiderPortal: case T::SwingPortal:
                return Kind::ModePortal;
            case T::Breakable: case T::DualPortal: case T::SoloPortal: case T::TeleportPortal:
            case T::CustomRing: case T::TeleportOrb:
                return Kind::Taint;
            default:
                return Kind::Ignore;
        }
    }

    void ensureSpeedTable(PlayLayer* pl) {
        if (g_speedLayer == pl) return;
        g_speedLayer = pl;
        g_speedPortals.clear();
        if (!pl || !pl->m_objects) return;
        for (auto obj : CCArrayExt<GameObject*>(pl->m_objects)) {
            float speed;
            switch (obj->m_objectID) {
                case 200: speed = 0.7f; break;
                case 201: speed = 0.9f; break;
                case 202: speed = 1.1f; break;
                case 203: speed = 1.3f; break;
                case 1334: speed = 1.6f; break;
                default: continue;
            }
            if (auto eo = typeinfo_cast<EffectGameObject*>(obj); eo && (eo->m_isSpawnTriggered || eo->m_isTouchTriggered)) continue;
            g_speedPortals.push_back({ obj->getPositionX(), speed });
        }
        std::sort(g_speedPortals.begin(), g_speedPortals.end(), [](auto& a, auto& b) { return a.x < b.x; });
    }

    size_t firstSpeedPortalAfter(float x) {
        auto it = std::upper_bound(g_speedPortals.begin(), g_speedPortals.end(), x,
            [](float v, SpeedPortal const& s) { return v < s.x; });
        return (size_t)(it - g_speedPortals.begin());
    }

    struct BandScope {
        GJBaseGameLayer* layer;
        bool on;
        float height = 0, center = 0, lastTop = 0;
        bool computed = false, freeMode = false, noSnap = false;

        BandScope(GJBaseGameLayer* l, BandView const& v) : layer(l), on(v.active) {
            if (!on) return;
            auto& gs = layer->m_gameState;
            height = gs.m_lastModeHeight;
            center = gdx::at<float>(layer, gdx::kBandCenter);
            computed = gs.m_unkBool1;
            freeMode = gs.m_isFreeMode;
            noSnap = gs.m_disableCameraGridSnap;
            lastTop = gdx::at<float>(layer, gdx::kBandLastTop);

            gs.m_lastModeHeight = v.height;
            gdx::at<float>(layer, gdx::kBandCenter) = v.center;
            gs.m_unkBool1 = true;
            gs.m_isFreeMode = v.freeMode;
            gs.m_disableCameraGridSnap = v.noSnap;
            gdx::at<float>(layer, gdx::kBandLastTop) = gdx::bandTop(layer);
        }

        ~BandScope() {
            if (!on) return;
            auto& gs = layer->m_gameState;
            gs.m_lastModeHeight = height;
            gdx::at<float>(layer, gdx::kBandCenter) = center;
            gs.m_unkBool1 = computed;
            gs.m_isFreeMode = freeMode;
            gs.m_disableCameraGridSnap = noSnap;
            gdx::at<float>(layer, gdx::kBandLastTop) = lastTop;
        }
    };

    struct SavedFlags {
        EnhancedGameObject* obj;
        bool any, p1, p2;
    };

    void showGhostView(Ghost& g, GameObject* obj, std::vector<SavedFlags>& saved) {
        auto eo = typeinfo_cast<EnhancedGameObject*>(obj);
        if (!eo) return;
        saved.push_back({ eo, gdx::at<bool>(eo, gdx::kActivatedAny), eo->m_activatedByPlayer1, eo->m_activatedByPlayer2 });
        bool used = eo->m_activatedByPlayer1;
        for (auto& u : g.used) {
            if (u.obj == eo) {
                used = u.used;
                break;
            }
        }
        eo->m_activatedByPlayer2 = used;
    }

    void keepGhostView(Ghost& g, std::vector<SavedFlags> const& saved) {
        for (auto& s : saved) {
            bool used = s.obj->m_activatedByPlayer2;
            auto it = std::find_if(g.used.begin(), g.used.end(), [&](Activation const& u) { return u.obj == s.obj; });
            if (it != g.used.end()) it->used = used;
            else if (used != s.p1) g.used.push_back({ s.obj, used });
        }
        for (auto it = saved.rbegin(); it != saved.rend(); ++it) {
            gdx::at<bool>(it->obj, gdx::kActivatedAny) = it->any;
            it->obj->m_activatedByPlayer1 = it->p1;
            it->obj->m_activatedByPlayer2 = it->p2;
        }
    }

    float bandHeight(GameObjectType t) {
        using T = GameObjectType;
        switch (t) {
            case T::ShipPortal: case T::UfoPortal: case T::WavePortal: case T::SwingPortal: return 300.f;
            case T::BallPortal: return 240.f;
            default: return 270.f;
        }
    }

    void enterModePortal(Ghost& g, GameObject* portal) {
        auto p = g.p;
        auto eo = typeinfo_cast<EffectGameObject*>(portal);
        if (eo && !g_layer->canBeActivatedByPlayer(p, eo)) return;

        using T = GameObjectType;
        auto type = portal->m_objectType;
        bool banded = type == T::ShipPortal || type == T::UfoPortal || type == T::WavePortal || type == T::SwingPortal
            || type == T::BallPortal || type == T::SpiderPortal;
        if (banded) {
            auto& gs = g_layer->m_gameState;
            bool freeMode = g.band.active ? g.band.freeMode : gs.m_isFreeMode;
            bool noSnap = g.band.active ? g.band.noSnap : gs.m_disableCameraGridSnap;
            if (gdx::at<int>(portal, gdx::kPortalOwnCamera) == 1) {
                freeMode = gdx::at<bool>(portal, gdx::kPortalFreeMode);
                noSnap = gdx::at<bool>(portal, gdx::kPortalNoSnap);
            }
            float h = bandHeight(type);
            float low = portal->getPositionY() - h * 0.5f;
            if (!noSnap) low = std::floor(low / 30.f) * 30.f;
            g.band = { true, h, std::max(90.f, low) + h * 0.5f, freeMode, noSnap };
        } else {
            g.band = {};
        }

        p->m_lastPortalPos = portal->getPosition();
        p->m_lastActivatedPortal = portal;
        bool noFx = portal->m_hasNoEffects;
        switch (type) {
            case T::ShipPortal:
                p->switchedToMode(type);
                p->toggleFlyMode(true, noFx);
                break;
            case T::UfoPortal:
                p->switchedToMode(type);
                p->toggleBirdMode(true, noFx);
                break;
            case T::WavePortal:
                p->switchedToMode(type);
                p->toggleDartMode(true, noFx);
                break;
            case T::SwingPortal:
                p->switchedToMode(type);
                p->toggleSwingMode(true, noFx);
                break;
            case T::CubePortal:
                p->toggleFlyMode(false, false);
                p->toggleBirdMode(false, false);
                p->toggleRollMode(false, false);
                p->toggleDartMode(false, false);
                p->toggleRobotMode(false, false);
                p->toggleSpiderMode(false, false);
                p->toggleSwingMode(false, false);
                p->updatePlayerArt();
                p->updateDashArt();
                break;
            case T::BallPortal:
                p->toggleFlyMode(false, false);
                p->toggleBirdMode(false, false);
                p->toggleDartMode(false, false);
                p->toggleRobotMode(false, false);
                p->toggleSpiderMode(false, false);
                p->toggleSwingMode(false, false);
                p->toggleRollMode(true, noFx);
                break;
            case T::RobotPortal:
                p->toggleFlyMode(false, false);
                p->toggleBirdMode(false, false);
                p->toggleRollMode(false, false);
                p->toggleDartMode(false, false);
                p->toggleSpiderMode(false, false);
                p->toggleSwingMode(false, false);
                p->toggleRobotMode(true, noFx);
                break;
            case T::SpiderPortal:
                p->toggleFlyMode(false, false);
                p->toggleBirdMode(false, false);
                p->toggleRollMode(false, false);
                p->toggleDartMode(false, false);
                p->toggleRobotMode(false, false);
                p->toggleSwingMode(false, false);
                p->toggleSpiderMode(true, noFx);
                break;
            default:
                break;
        }
        portal->activatedByPlayer(p);
        g_portal = true;
    }
}

bool isHeld(PlayerObject* p) {
    auto it = p->m_holdingButtons.find((int)PlayerButton::Jump);
    return it != p->m_holdingButtons.end() && it->second;
}

class $modify(Rq0, PlayLayer) {
    struct Fields {
        std::vector<Ref<PlayerObject>> ghosts;
        Ref<CCLayer> ghostParent;
    };

    static void onModify(auto& self) {
        (void)self.setHookPriority("PlayLayer::destroyPlayer", Priority::Stub);
        (void)self.setHookPriority("PlayLayer::playGravityEffect", Priority::Stub);
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        bool spike = object && object == m_anticheatSpike;
        if (sim::active) {
            if (!spike) {
                g_died = true;
                sim::killer = object;
            }
            return;
        }
        if (!spike) bot::onRealDeath(player, object);
        PlayLayer::destroyPlayer(player, object);
    }

    void playGravityEffect(bool flip) {
        if (sim::active) return;
        PlayLayer::playGravityEffect(flip);
    }

    void resetLevel() {
        input::clearPending();
        bot::onAttemptEnd(this);
        PlayLayer::resetLevel();
    }

    void onQuit() {
        input::clearPending();
        bot::onAttemptEnd(this);
        bot::onLevelExit();
        traj::reset();
        g_speedLayer = nullptr;
        PlayLayer::onQuit();
    }

    void showNewBest(bool newReward, int orbs, int diamonds, bool demonKey, bool noRetry, bool noTitle) {
        if (bot::blocksSaving()) return;
        PlayLayer::showNewBest(newReward, orbs, diamonds, demonKey, noRetry, noTitle);
    }
};

PlayerObject* getGhost(PlayLayer* pl, size_t index) {
    auto& f = static_cast<Rq0*>(pl)->m_fields;
    if (!f->ghostParent) f->ghostParent = CCLayer::create();
    while (f->ghosts.size() <= index) {
        auto ghost = PlayerObject::create(1, 1, pl, f->ghostParent, true);
        if (!ghost) return nullptr;
        ghost->setVisible(false);
        f->ghostParent->addChild(ghost);
        f->ghosts.push_back(ghost);
    }
    return f->ghosts[index];
}

static void clonePlayer(PlayerObject* d, PlayerObject* s) {
    d->copyAttributes(s);

#define C(m) d->m = s->m
    C(m_wasTeleported); C(m_fixGravityBug); C(m_reverseSync);
    C(m_yVelocityBeforeSlope); C(m_dashX); C(m_dashY); C(m_dashAngle); C(m_dashStartTime); C(m_dashRing);
    C(m_slopeStartTime); C(m_justPlacedStreak); C(m_maybeLastGroundObject);
    C(m_lastCollisionBottom); C(m_lastCollisionTop); C(m_lastCollisionLeft); C(m_lastCollisionRight);
    C(m_unk50C); C(m_unk510);
    C(m_currentSlope2); C(m_preLastGroundObject); C(m_slopeAngle); C(m_slopeSlidingMaybeRotated); C(m_quickCheckpointMode);
    C(m_collidedObject); C(m_lastGroundObject); C(m_collidingWithLeft); C(m_collidingWithRight); C(m_maybeSavedPlayerFrame);
    C(m_scaleXRelated2); C(m_groundYVelocity); C(m_yVelocityRelated); C(m_scaleXRelated3);
    C(m_scaleXRelated4); C(m_scaleXRelated5); C(m_isCollidingWithSlope);
    C(m_isBallRotating); C(m_unk669); C(m_currentPotentialSlope); C(m_currentSlope); C(unk_584);
    C(m_collidingWithSlopeId); C(m_slopeFlipGravityRelated); C(m_slopeAngleRadians);
    C(m_rotateObjectsRelated); C(m_potentialSlopeMap);
    C(m_rotationSpeed); C(m_rotateSpeed); C(m_isRotating); C(m_isBallRotating2);
    C(m_speedMultiplier); C(m_yStart); C(m_gravity); C(m_gameModeChangedTime);
    C(m_padRingRelated); C(m_maybeReducedEffects); C(m_maybeIsFalling); C(m_maybeCanRunIntoBlocks);
    C(m_isOnGround3); C(m_checkpointTimeout);
    C(m_lastCheckpointTime); C(m_lastJumpTime); C(m_lastFlipTime); C(m_lastSpiderFlipTime);
    C(m_accelerationOrSpeed); C(m_snapDistance); C(m_ringJumpRelated); C(m_ringRelatedSet);
    C(m_objectSnappedTo); C(m_onFlyCheckpointTries);
    C(m_slopeRotation); C(m_currentSlopeYVelocity); C(m_unk3d0); C(m_blackOrbRelated);
    C(m_unk3e0); C(m_unk3e1); C(m_isAccelerating); C(m_isCurrentSlopeTop);
    C(m_collidedTopMinY); C(m_collidedBottomMaxY); C(m_collidedLeftMaxX); C(m_collidedRightMinX);
    C(m_maybeIsColliding); C(m_jumpBuffered); C(m_stateRingJump); C(m_wasJumpBuffered); C(m_wasRobotJump);
    C(m_stateJumpBuffered); C(m_stateRingJump2); C(m_touchedRing); C(m_touchedCustomRing);
    C(m_touchedGravityPortal); C(m_maybeTouchedBreakableBlock); C(m_jumpRelatedAC2); C(m_touchedPad);
    C(m_yVelocity); C(m_fallSpeed); C(m_isOnSlope); C(m_wasOnSlope); C(m_slopeVelocity); C(m_maybeUpsideDownSlope);
    C(m_isShip); C(m_isBird); C(m_isBall); C(m_isDart); C(m_isRobot); C(m_isSpider);
    C(m_isUpsideDown); C(m_isDead); C(m_isOnGround); C(m_isGoingLeft); C(m_isSideways); C(m_isSwing);
    C(m_reverseRelated); C(m_maybeReverseSpeed); C(m_maybeReverseAcceleration); C(m_xVelocityRelated2);
    C(m_isDashing); C(m_dashFireFrame); C(m_groundObjectMaterial); C(m_vehicleSize); C(m_playerSpeed);
    C(m_shipRotation); C(m_lastPortalPos); C(m_unkUnused3); C(m_isOnGround2); C(m_lastLandTime);
    C(m_platformerVelocityRelated); C(m_maybeIsBoosted); C(m_scaleXRelatedTime); C(m_decreaseBoostSlide);
    C(m_unkA29); C(m_isLocked); C(m_controlsDisabled); C(m_lastGroundedPos);
    C(m_touchedRings); C(m_lastActivatedPortal); C(m_hasEverJumped); C(m_hasEverHitRing);
    C(m_position); C(m_isSecondPlayer); C(m_unkA99); C(m_totalTime); C(m_isBeingSpawnedByDualPortal);
    C(m_unkAngle1); C(m_yVelocityRelated3); C(m_followRelated); C(m_unk838);
    C(m_stateOnGround); C(m_stateUnk); C(m_stateNoStickX); C(m_stateNoStickY); C(m_stateUnk2);
    C(m_stateBoostX); C(m_stateBoostY); C(m_maybeStateForce2); C(m_stateScale);
    C(m_platformerXVelocity); C(m_holdingRight); C(m_holdingLeft); C(m_leftPressedFirst); C(m_scaleXRelated);
    C(m_maybeHasStopped); C(m_xVelocityRelated); C(m_maybeGoingCorrectSlopeDirection); C(m_isSliding);
    C(m_maybeSlopeForce); C(m_isOnIce); C(m_physDeltaRelated); C(m_isOnGround4); C(m_maybeSlidingTime);
    C(m_maybeSlidingStartTime); C(m_changedDirectionsTime); C(m_slopeEndTime); C(m_isMoving);
    C(m_platformerMovingLeft); C(m_platformerMovingRight); C(m_isSlidingRight); C(m_maybeChangedDirectionAngle);
    C(m_isPlatformer);
    C(m_stateNoAutoJump); C(m_stateDartSlide); C(m_stateHitHead); C(m_stateFlipGravity); C(m_gravityMod);
    C(m_stateForce); C(m_stateForceVector); C(m_affectedByForces);
    C(m_jumpPadRelated); C(m_lastMovedTime); C(m_playerSpeedAC); C(m_fixRobotJump); C(m_holdingButtons);
    C(m_inputsLocked); C(m_gv0123); C(m_isOutOfBounds); C(m_fallStartY); C(m_disablePlayerSqueeze);
    C(m_ignoreDamage); C(m_enable22Changes); C(m_enableImpulseFix);
#undef C

    gdx::at<bool>(d, gdx::kMultiActivateArg) = gdx::at<bool>(s, gdx::kMultiActivateArg);

    d->m_touchingRings->removeAllObjects();
    if (s->m_touchingRings) d->m_touchingRings->addObjectsFromArray(s->m_touchingRings);
    gdx::at<float>(d, gdx::kSizeScaleX) = gdx::at<float>(s, gdx::kSizeScaleX);
    gdx::at<float>(d, gdx::kSizeScaleY) = gdx::at<float>(s, gdx::kSizeScaleY);
    d->setScaleX(s->getScaleX());
    d->setScaleY(s->getScaleY());
    d->setPosition(s->getPosition());
    d->setRotation(s->getRotation());

    d->m_shouldTryPlacingCheckpoint = false;
    d->m_canPlaceCheckpoint = false;
    d->m_pendingCheckpoint = nullptr;
    d->m_playEffects = false;
}

void copyGhost(Ghost& d, Ghost const& s) {
    clonePlayer(d.p, s.p);
    d.held = s.held;
    d.used = s.used;
    d.touchPrev = s.touchPrev;
    d.touchNow.clear();
    d.band = s.band;
    d.nextSpeed = s.nextSpeed;
    d.clear = s.clear;
    d.penalty = s.penalty;
}

void ghostFromReal(Ghost& d, PlayerObject* real) {
    clonePlayer(d.p, real);
    d.held = isHeld(real);
    d.used.clear();
    d.touchPrev.clear();
    d.touchNow.clear();
    d.band = {};
    d.clear = 1e9f;
    d.penalty = 0.f;
    if (auto layer = real->m_gameLayer) {
        auto& gs = layer->m_gameState;
        int uid = real->m_uniqueID;
        unsigned tick = gs.m_commandIndex;
        for (auto& [key, when] : gs.m_activatedObjectIDs) {
            if (key.second == uid && (unsigned)when + 1 >= tick) d.touchPrev.push_back(key.first);
        }
    }
    ensureSpeedTable(PlayLayer::get());
    d.nextSpeed = firstSpeedPortalAfter(real->getPositionX());
}

void simBegin(PlayLayer* pl) {
    g_layer = pl;
    sim::active = true;
    auto layer = static_cast<GJBaseGameLayer*>(pl);
    g_damageCheck = gdx::at<bool>(layer, gdx::kDamageCheck);
    g_damageSkip = gdx::at<bool>(layer, gdx::kDamageSkip);
    ensureSpeedTable(pl);
}

void simEnd() {
    sim::active = false;
    g_cur = nullptr;
}

StepResult simStep(Ghost& g, bool hold) {
    auto p = g.p;
    float dt = sim::dt;
    g_cur = &g;
    g_died = g_tainted = g_portal = false;
    sim::killer = nullptr;

    float x = p->getPositionX();
    while (g.nextSpeed < g_speedPortals.size() && g_speedPortals[g.nextSpeed].x <= x) {
        p->updateTimeMod(g_speedPortals[g.nextSpeed].speed, true);
        g.nextSpeed++;
    }

    if (hold != g.held) {
        if (hold) p->pushButton(PlayerButton::Jump);
        else p->releaseButton(PlayerButton::Jump);
        g.held = hold;
    }

    p->resetTouchedRings(false);
    p->m_collisionLogTop->removeAllObjects();
    p->m_collisionLogBottom->removeAllObjects();
    p->m_collisionLogLeft->removeAllObjects();
    p->m_collisionLogRight->removeAllObjects();
    p->m_unk50C = p->m_lastCollisionBottom;
    p->m_unk510 = p->m_lastCollisionTop;
    p->m_lastCollisionBottom = p->m_lastCollisionTop = -1;
    p->m_lastCollisionLeft = p->m_lastCollisionRight = -1;

    {
        BandScope band(g_layer, g.band);
        p->updateInternalActions(dt);
        p->update(dt);
        int r = g_layer->checkCollisions(p, dt, g_damageCheck || g_damageSkip);
        if (r == 1 && g_damageCheck && !g_damageSkip) g_died = true;
    }
    g.touchPrev.swap(g.touchNow);
    g.touchNow.clear();

    if (g_died || p->m_isDead) return StepResult::Died;
    if (g_tainted) return StepResult::Tainted;
    p->updateRotation(dt);
    p->m_shipRotation = p->getPosition();
    p->m_totalTime += dt / 60.0;
    return g_portal ? StepResult::Portal : StepResult::Alive;
}

class $modify(Rq2, GJBaseGameLayer) {
    static void onModify(auto& self) {
        (void)self.setHookPriority("GJBaseGameLayer::playerTouchedRing", Priority::Stub);
        (void)self.setHookPriority("GJBaseGameLayer::gameEventTriggered", Priority::Stub);
        (void)self.setHookPriority("GJBaseGameLayer::canBeActivatedByPlayer", Priority::Stub);
    }

    bool canBeActivatedByPlayer(PlayerObject* player, EffectGameObject* object) {
        if (!isGhost(player) || !object) return GJBaseGameLayer::canBeActivatedByPlayer(player, object);
        auto& g = *g_cur;
        int id = object->m_uniqueID;
        bool wasTouching = std::find(g.touchPrev.begin(), g.touchPrev.end(), id) != g.touchPrev.end();
        g.touchNow.push_back(id);
        if (object->canMultiActivate(gdx::at<bool>(player, gdx::kMultiActivateArg)) && wasTouching) return false;
        return !object->hasBeenActivatedByPlayer(player);
    }

    void collisionCheckObjects(PlayerObject* player, gd::vector<GameObject*>* objects, int objectCount, float dt) {
        if (!isGhost(player) || !objects) {
            GJBaseGameLayer::collisionCheckObjects(player, objects, objectCount, dt);
            return;
        }
        auto& g = *g_cur;
        static gd::vector<GameObject*> kept;
        static std::vector<SavedFlags> saved;
        static std::vector<GameObject*> portals;
        kept.clear();
        saved.clear();
        portals.clear();

        CCRect ghostRect = player->getObjectRect();
        auto touches = [&](GameObject* obj) {
            return obj->getObjectRect().intersectsRect(ghostRect);
        };
        bool flying = player->m_isShip || player->m_isBird || player->m_isDart || player->m_isSwing;
        float gx0 = ghostRect.origin.x, gx1 = gx0 + ghostRect.size.width;
        float gy0 = ghostRect.origin.y, gy1 = gy0 + ghostRect.size.height;
        auto measureRect = [&](CCRect const& r) {
            float x0 = r.origin.x, x1 = x0 + r.size.width;
            if (x1 < gx0 - 4.f) return;
            float y0 = r.origin.y, y1 = y0 + r.size.height;
            float dx = std::max({ 0.f, x0 - gx1, gx0 - x1 });
            float dy = std::max({ 0.f, y0 - gy1, gy0 - y1 });
            g.clear = std::min(g.clear, std::max(dx, dy));
        };
        auto measure = [&](GameObject* obj) { measureRect(obj->getObjectRect()); };

        int count = std::min(objectCount, (int)objects->size());
        for (int i = 0; i < count; i++) {
            auto obj = (*objects)[i];
            if (!obj || obj == m_anticheatSpike) continue;
            auto type = obj->m_objectType;
            switch (classify(type)) {
                case Kind::Keep:
                    if (sim::scoring && (type == GameObjectType::Hazard || type == GameObjectType::AnimatedHazard
                        || (flying && type == GameObjectType::Solid))) {
                        measure(obj);
                    }
                    kept.push_back(obj);
                    break;
                case Kind::Stateful:
                    showGhostView(g, obj, saved);
                    kept.push_back(obj);
                    break;
                case Kind::ModePortal:
                    if (touches(obj)) {
                        showGhostView(g, obj, saved);
                        portals.push_back(obj);
                    }
                    break;
                case Kind::Taint:
                    if (touches(obj)) g_tainted = true;
                    break;
                default:
                    break;
            }
        }
        if (sim::scoring) {
            for (auto& ph : learn::phantoms()) {
                if (ph.origin.x + ph.size.width < gx0 - 4.f || ph.origin.x > gx1 + 90.f) continue;
                if (ph.intersectsRect(ghostRect)) g.penalty += learn::kPhantomCost;
            }
        }
        GJBaseGameLayer::collisionCheckObjects(player, &kept, (int)kept.size(), dt);
        for (auto portal : portals) enterModePortal(g, portal);
        keepGhostView(g, saved);
    }

    void playerTouchedRing(PlayerObject* player, RingObject* ring) {
        if (!isGhost(player)) {
            GJBaseGameLayer::playerTouchedRing(player, ring);
            return;
        }
        if (!player->m_touchingRings->containsObject(ring)) player->m_touchingRings->addObject(ring);
        player->m_touchedRings.insert(ring->m_uniqueID);
        bool noAuto = gdx::at<bool>(ring, gdx::kRingNoAuto);
        if (!player->m_isShip && !player->m_isBird && !player->m_isDart && !player->m_isSwing && !noAuto) {
            player->ringJump(ring, false);
        }
    }

    void gameEventTriggered(GJGameEvent event, int material, int playerID) {
        if (sim::active) return;
        GJBaseGameLayer::gameEventTriggered(event, material, playerID);
    }
};

class $modify(Rq3, PlayerObject) {
    void update(float dt) {
        if (!sim::active && !sim::halfTick && dt > 0.f) {
            auto pl = PlayLayer::get();
            if (pl && this == pl->m_player1) sim::dt = dt;
        }
        PlayerObject::update(dt);
    }

    void incrementJumps() {
        if (sim::active) return;
        PlayerObject::incrementJumps();
    }

    void spawnCircle() {
        if (sim::active) return;
        PlayerObject::spawnCircle();
    }

    void spawnDualCircle() {
        if (sim::active) return;
        PlayerObject::spawnDualCircle();
    }

    void spawnScaleCircle() {
        if (sim::active) return;
        PlayerObject::spawnScaleCircle();
    }

    void spawnPortalCircle(ccColor3B color, float startRadius) {
        if (sim::active) return;
        PlayerObject::spawnPortalCircle(color, startRadius);
    }
};

class $modify(Rq5, HardStreak) {
    void addPoint(CCPoint point) {
        if (sim::active) return;
        HardStreak::addPoint(point);
    }
};

class $modify(Rq6, GameObject) {
    void playShineEffect() {
        if (sim::active) return;
        GameObject::playShineEffect();
    }
};
