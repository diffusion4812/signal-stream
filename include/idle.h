#pragma once

#include "appstate.h"

static const uint64_t idleThresholdNS = 30 * 1E9;
static const uint64_t minFrameTimeNS = 2000000; // 2ms minimum frame time to avoid busy-waiting
static const double activeFPS = 200.0;
static const double idleFPS = 5.0;

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
    uint64_t targetFrameTimeNS = static_cast<uint64_t>(1E9 / targetFPS);

    auto now = SDL_GetTicksNS();
    auto idleDur = now - state->lastInputTimestamp;
    if (!state->isIdle && idleDur >= idleThresholdNS) {
        state->isIdle = true;
        SDL_Log("Entering idle mode (no user input for %lld ms)", idleDur / 1E6);
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