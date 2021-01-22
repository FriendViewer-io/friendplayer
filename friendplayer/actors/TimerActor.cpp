#include "actors/TimerActor.h"

#include <Windows.h>
#include <mmiscapi2.h>

class WinMMTimer {
public:
    static void CALLBACK TimerCall(UINT, UINT, DWORD_PTR user, DWORD_PTR, DWORD_PTR) {
        TimerActor::TimerCapture* inst = reinterpret_cast<TimerActor::TimerCapture*>(user);
        inst->this_actor->SendTimerFire(false, inst->timestamp);
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
        fp_actor::FireTimer fire_msg;
        msg.UnpackTo(&fire_msg);
        if (fire_msg.timer_timestamp() >= ignore_before) {    
            if (!is_periodic) {
                timer_handle = 0;
                delete capture;
                capture = nullptr;
            }
            OnTimerFire();
        }
    } else {
        Actor::OnMessage(msg);
    }
}

void TimerActor::SetTimerInternal(uint32_t period_ms, bool periodic) {
    StopTimer();
    is_periodic = periodic;
    UINT flags = TIME_KILL_SYNCHRONOUS;
    flags |= (periodic) ? TIME_PERIODIC : TIME_ONESHOT;
    capture = new TimerCapture{this, static_cast<uint64_t>(clock::now().time_since_epoch().count()), periodic};
    timer_handle = timeSetEvent(static_cast<UINT>(period_ms),
        0, &WinMMTimer::TimerCall, reinterpret_cast<DWORD_PTR>(capture), flags);
}

void TimerActor::StopTimer() {
    // Kill any current timer
    if (timer_handle) {
        timeKillEvent(timer_handle);
    }
    timer_handle = 0;
    if (is_periodic && capture != nullptr) {
        delete capture;
        capture = nullptr;
    }
    IgnoreBeforeNow();
}

void TimerActor::SendTimerFire(bool disarm, uint64_t timer_timestamp) {
    if (disarm) {
        StopTimer();
    }
    fp_actor::FireTimer msg;
    google::protobuf::Any any_msg;
    msg.set_timer_timestamp(timer_timestamp);
    any_msg.PackFrom(msg);
    EnqueueMessage(std::move(any_msg));
}

void TimerActor::IgnoreBeforeNow() {
    ignore_before = clock::now().time_since_epoch().count();
}

TimerActor::~TimerActor() {
    // Ensure timer is stopped so WinMM CB thread isn't using bad pointer
    StopTimer();
}