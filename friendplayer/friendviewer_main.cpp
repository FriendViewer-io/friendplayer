#include "actors/ActorEnvironment.h"
#include "actors/CommonActorNames.h"

#include "common/Config.h"
#include "common/Log.h"

#include "protobuf/actor_messages.pb.h"

int main(int argc, char** argv) {
    using namespace std::chrono_literals;

    if (Config::LoadConfig(argc, argv) >= 0)
        return 1;

    Log::init_stdout_logging(LogOptions{Config::EnableTracing});

    ActorEnvironment env;
    google::protobuf::Any any_msg;

    fp_actor::SocketInit socket_init;
    socket_init.set_port(Config::Port);
    std::string socket_type;
    if (!Config::IsHost) {
        socket_init.set_ip(Config::ServerIP);
        socket_type = "ClientSocketActor";
    } else {
        socket_type = "HostSocketActor";
    }
    any_msg.PackFrom(socket_init);
    env.AddActor(socket_type, SOCKET_ACTOR_NAME, std::make_optional(std::move(any_msg)));

    if (!Config::IsHost) {
        fp_actor::ClientClientManagerInit client_mgr_init;
        client_mgr_init.set_host_ip(Config::ServerIP);
        client_mgr_init.set_host_port(Config::Port);
        any_msg.PackFrom(client_mgr_init);
    } else {
        fp_actor::InputInit input_init;
        input_init.set_reuse_controllers(Config::SaveControllers);
        any_msg.PackFrom(input_init);
        env.AddActor("InputActor", INPUT_ACTOR_NAME, any_msg);

        env.AddActor("HostSettingsActor", SETTINGS_ACTOR_NAME);

        fp_actor::HostClientManagerInit client_mgr_init;
        for (int monitor_index : Config::MonitorIndecies) {
            client_mgr_init.add_monitor_indices(monitor_index);
        }
        client_mgr_init.set_num_audio_streams(1);
        client_mgr_init.set_port(Config::Port);
        any_msg.PackFrom(client_mgr_init);
    }
    env.AddActor("ClientManagerActor", CLIENT_MANAGER_ACTOR_NAME, std::make_optional(std::move(any_msg)));

    fp_actor::HeartbeatActorInit heartbeat_init;
    heartbeat_init.set_timeout_ms(10000);
    heartbeat_init.set_heartbeat_send_ms(2000);
    any_msg.PackFrom(heartbeat_init);
    env.AddActor("HeartbeatActor", HEARTBEAT_ACTOR_NAME, std::make_optional(std::move(any_msg)));

    env.StartEnvironment();
}