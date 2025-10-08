#pragma once
3
inline void NotifyUserInput(void* userdata)
{
    AppState* state = (AppState*)userdata;
    state->lastInputTimestamp = SDL_GetTicksNS();
    state->isIdle = false;
}

inline void NotifyAppInput(void* userdata)
{
    AppState* state = (AppState*)userdata;
    state->lastInputTimestamp = SDL_GetTicksNS();
    state->isIdle = false;
}

void IdleMode_HandleFrameThrottling(void* userdata) {
    AppState* state = (AppState*)userdata;
    auto targetFPS = state->isIdle ? idleFPS : activeFPS;
    uint64_t targetFrameTimeNS = 1000000000 / targetFPS;

    auto now = SDL_GetTicksNS();
    auto idleDur = now - state->lastInputTimestamp;
    if (!state->isIdle && idleDur >= idleThresholdNS) {
        state->isIdle = true;
        SDL_Log("Entering idle mode (no user input for %lld ms)", idleDur / 1000000);
    }

    auto elapsedNS = now - state->lastTime;

    if (elapsedNS < targetFrameTimeNS) {
        uint64_t sleepNS = targetFrameTimeNS - elapsedNS;
        if (sleepNS > minFrameTimeNS) {
            SDL_DelayPrecise(sleepNS);
        }
    }

    now = SDL_GetTicksNS();
    elapsedNS = now - state->lastTime;
    state->fps = (elapsedNS > 0) ? 1000000000.0 / elapsedNS : 0.0;

    state->lastTime = SDL_GetTicksNS();
}