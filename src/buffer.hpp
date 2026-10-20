#pragma once

class PlayLayer;

namespace buffer {
    int decideDelay(PlayLayer* pl, int stage, bool down, int holdTicks);

    bool shouldAutoRelease(PlayLayer* pl, int holdTicks);
}
