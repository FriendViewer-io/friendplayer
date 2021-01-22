#include "actors/BaseActor.h"

#include "protobuf/actor_messages.pb.h"
#include "actors/ActorMap.h"
#include "common/Log.h"

BaseActor::BaseActor(const ActorMap& actor_map, DataBufferMap& buffer_map, std::string&& name)
  : actor_map(actor_map),
    buffer_map(buffer_map),
    name(std::move(name)),
    actor_thread(nullptr),
    is_running(false),
    init_msg(std::nullopt) { }

void BaseActor::MessageLoop() {
    OnInit(init_msg);
    while (is_running) {
        google::protobuf::Any msg;
        actor_msg_queue.wait_dequeue(msg);
        OnMessage(msg);
    }
    OnFinish();
}

void BaseActor::SendTo(std::string_view target, const generic_msg& msg) {
    any_msg any;
    any.PackFrom(msg);
    SendTo(target, std::move(any));
}

void BaseActor::SendTo(std::string_view target, any_msg&& msg) {
    actor_map.FindActor(target, [this, any = std::move(msg)] (BaseActor* target) {
        target->EnqueueMessage(std::move(const_cast<any_msg&>(any)));
    });
}

void BaseActor::EnqueueMessage(any_msg&& msg) {
    actor_msg_queue.enqueue(std::move(msg));
}

void BaseActor::StartActor() {
    is_running = true;
    actor_thread = std::make_unique<std::thread>(&BaseActor::MessageLoop, this);
}

BaseActor::~BaseActor() {
    actor_thread->join();
}