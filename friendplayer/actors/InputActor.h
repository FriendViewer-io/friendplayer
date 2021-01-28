#pragma once

#include "actors/Actor.h"

#include "protobuf/actor_messages.pb.h"
#include "protobuf/network_messages.pb.h"

class InputStreamer;

class InputActor : public Actor {
public:
    InputActor(const ActorMap& actor_map, DataBufferMap& buffer_map, std::string&& name)
      : Actor(actor_map, buffer_map, std::move(name)), input_streamer(nullptr) {}

    void OnMessage(const any_msg& msg) override;
    void OnInit(const std::optional<any_msg>& init_msg) override;
    void OnFinish() override;

private:
    InputStreamer* input_streamer;

    std::map<int, void*> stream_num_to_monitor;

    void OnInputData(fp_actor::InputData& frame);

    void OnKeyboardFrame(std::string& actor_name, const fp_network::KeyboardFrame& msg);
    void OnMouseFrame(std::string& actor_name, const fp_network::MouseFrame& msg);
    void OnControllerFrame(std::string& actor_name, const fp_network::ControllerFrame& msg);
};

DEFINE_ACTOR_GENERATOR(InputActor)