#pragma once
class Engine {
public:
    bool is_in_game() {
        void** vt = *(void***)this;
        return ((bool (*)(void*))vt[33])(this);
    }
};
inline Engine* engine = nullptr;
