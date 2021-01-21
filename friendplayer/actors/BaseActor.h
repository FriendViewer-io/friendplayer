#pragma once

#include <concurrentqueue/blockingconcurrentqueue.h>
#include <google/protobuf/any.pb.h>

#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include "actors/ActorType.h"
#include "actors/ActorMap.h"
#include "actors/DataBuffer.h"

using generic_msg = google::protobuf::Message;
using any_msg = google::protobuf::Any;

class BaseActor {
public:
    BaseActor(const ActorMap& actor_map, DataBufferMap& buffer_map, std::string&& name);

    void SendTo(std::string_view target, const generic_msg& msg);
    void SendTo(std::string_view target, any_msg&& msg);
    const std::string& GetName() const { return name; }
    void SetInitMessage(const any_msg& init) { init_msg = init; }
    void EnqueueMessage(any_msg&& msg);

    // Starts thread for this actor
    virtual void StartActor();
    // Called in MessageLoop before first dequeue
    virtual void OnInit(const std::optional<any_msg>& init_msg) = 0;
    // Called for each message received
    virtual void OnMessage(const any_msg& msg) = 0;
    // Core message 
    virtual void MessageLoop();
    // Called after last message is processed and MessageLoop exits
    virtual void OnFinish() = 0;

    virtual ~BaseActor();

protected:
    bool is_running;
    DataBufferMap& buffer_map;

private:
    // Outlives all actors, readonly
    const ActorMap& actor_map;
    const std::string name;
    moodycamel::BlockingConcurrentQueue<any_msg> actor_msg_queue;
    std::unique_ptr<std::thread> actor_thread;

    std::optional<any_msg> init_msg;
};

DEFINE_ACTOR_GENERATOR(BaseActor)
