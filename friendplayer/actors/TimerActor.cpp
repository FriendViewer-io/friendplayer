#include "actors/TimerActor.h"

#include <Windows.h>
#include <mmiscapi2.h>

class WinMMTimer {
public:
    static void CALLBACK TimerCall(UINT, UINT, DWORD_PTR user, DWORD_PTR, DWORD_PTR) {
        TimerActor* inst = reinterpret_cast<TimerActor*>(user);
        inst->SendTimerFire(false);
    }
};

void TimerActor::OnMessage(const any_msg& msg) {
    if (msg.Is<fp_actor::StartTimer>()) {
        // Start a new timer
        fp_actor::StartTimer start_msg;
        msg.UnpackTo(&start_msg);
        if (!timer_handle) {
            SetTimerInternal(start_msg.period_ms(), start_msg.periodic());
        }
    } else if (msg.Is<fp_actor::StopTimer>()) {
        StopTimer();
    } else if (msg.Is<fp_actor::FireTimer>()) {
        fire_in_queue->store(false);
        if (!is_periodic) { timer_handle = 0; }
        OnTimerFire();
    } else {
        Actor::OnMessage(msg);
    }
}

void TimerActor::SetTimerInternal(uint32_t period_ms, bool periodic) {
    UINT flags = TIME_KILL_SYNCHRONOUS;
    flags |= (is_periodic) ? TIME_PERIODIC : TIME_ONESHOT;
    timer_handle = timeSetEvent(static_cast<UINT>(period_ms),
        0, &WinMMTimer::TimerCall, reinterpret_cast<DWORD_PTR>(this), flags);
}

void TimerActor::StopTimer() {
    // Kill any current timer
    if (timer_handle) {
        timeKillEvent(timer_handle);
    }
    timer_handle = 0;
}

void TimerActor::SendTimerFire(bool disarm) {
    if (fire_in_queue->exchange(true)) {
        // Fire already enqueued, avoid double-fire from MM thread & our own
        return;
    }
    if (disarm) {
        StopTimer();
    }
    fp_actor::FireTimer msg;
    google::protobuf::Any any_msg;
    any_msg.PackFrom(msg);
    EnqueueMessage(std::move(any_msg));
}

TimerActor::~TimerActor() {
    // Ensure timer is stopped so WinMM CB thread isn't using bad pointer
    StopTimer();
}