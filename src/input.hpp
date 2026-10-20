#pragma once

class GJBaseGameLayer;

namespace input {
    void clearPending();
    void apply(GJBaseGameLayer* gl, bool down);
}
