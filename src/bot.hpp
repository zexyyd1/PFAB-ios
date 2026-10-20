#pragma once

class GJBaseGameLayer;
class GameObject;
class PlayerObject;

namespace bot {
    extern bool on;
    extern bool showPath;

    void tick(GJBaseGameLayer* gl);
    bool ownsJump(GJBaseGameLayer* gl);

    void onRealDeath(PlayerObject* player, GameObject* object);
    void onAttemptEnd(GJBaseGameLayer* gl);
    void onLevelExit();
    bool blocksSaving();

    size_t learnedSpots();
    void forget();
}
