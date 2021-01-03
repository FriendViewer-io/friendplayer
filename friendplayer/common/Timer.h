#pragma once

#include <chrono>
#include <Windows.h>
#include <mmiscapi2.h>

#include "common/Log.h"

class Timer {
public:
    Timer() : timer_handle(0) {
        wake_event = CreateEvent(NULL, TRUE, FALSE, NULL);
    }

    void Start(long long period_us);
    bool Synchronize();
    void ResetCadence();
    void Stop();

private:
    static void CALLBACK TimerCB(UINT uTimerID, UINT uMsg, DWORD_PTR dwUser, DWORD_PTR dw1, DWORD_PTR dw2) {
        Timer* inst = reinterpret_cast<Timer*>(dwUser);

        auto before = std::chrono::system_clock::now();
        //while (inst->last_sync_point + inst->period > std::chrono::system_clock::now()) {}
        inst->last_sync_point = std::chrono::system_clock::now();
        //inst->ResetCadence();

        //LOG_INFO("TimerCB spent {} us spinning", std::chrono::duration_cast<std::chrono::microseconds>(inst->last_sync_point - before).count());
        SetEvent(inst->wake_event);
    }

    MMRESULT timer_handle;
    HANDLE wake_event;

    std::chrono::microseconds period;
    std::chrono::system_clock::time_point last_sync_point;
};
