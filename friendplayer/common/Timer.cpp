#include "Timer.h"

void Timer::Start(long long period_us) {
    period = std::chrono::microseconds(period_us);
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(period).count();

    timer_handle = timeSetEvent(std::chrono::duration_cast<std::chrono::milliseconds>(period).count(),
        0, &Timer::TimerCB, reinterpret_cast<DWORD_PTR>(this), TIME_PERIODIC);
    last_sync_point = std::chrono::system_clock::now();
}

bool Timer::Synchronize() {
    if (WaitForSingleObject(wake_event, INFINITE) != WAIT_OBJECT_0) {
        return false;
    }
    ResetEvent(wake_event);
    return true;
}

void Timer::ResetCadence() {
    if (timer_handle) {
        timeKillEvent(timer_handle);
    }

    timer_handle = timeSetEvent(std::chrono::duration_cast<std::chrono::milliseconds>(period).count(),
        0, &Timer::TimerCB, reinterpret_cast<DWORD_PTR>(this), TIME_PERIODIC | TIME_KILL_SYNCHRONOUS);
    last_sync_point = std::chrono::system_clock::now();
}

void Timer::Stop() {
    if (timer_handle) {
        timeKillEvent(timer_handle);
    }
}