#include "actors/ActorEnvironment.h"
#include "actors/CommonActorNames.h"

#include "common/Config.h"
#include "common/Log.h"

#include "protobuf/actor_messages.pb.h"
#include <puncher_messages.pb.h>
#include <minidumpapiset.h>

#include <DbgHelp.h>
#include <chrono>
#include <ctime>
#include <iomanip>
#pragma comment(lib, "DbgHelp.lib")

void CreateMiniDump(EXCEPTION_POINTERS* pep) {
    char dumpfile_name[64];
    auto time_t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::strftime(dumpfile_name, 63, "fp_crash_%Y-%m-%d_%H_%M_%S.dmp", std::localtime(&time_t));
    dumpfile_name[63] = 0;

    HANDLE dumpfile = CreateFile(dumpfile_name, GENERIC_READ | GENERIC_WRITE, 
        0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    if ((dumpfile != nullptr) && (dumpfile != INVALID_HANDLE_VALUE)) {
        MINIDUMP_EXCEPTION_INFORMATION mdei = { 0 }; 
        mdei.ThreadId = GetCurrentThreadId(); 
        mdei.ExceptionPointers = pep; 
        mdei.ClientPointers = TRUE; 

        BOOL rv = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), 
            dumpfile, MiniDumpNormal, (pep != nullptr) ? &mdei : nullptr, nullptr, nullptr);
        CloseHandle(dumpfile); 
    }
}

LONG WINAPI CrashdumpFilter(EXCEPTION_POINTERS *exception_info) {
    CreateMiniDump(exception_info);
    return EXCEPTION_EXECUTE_HANDLER;
}


int main(int argc, char** argv) {
    using namespace std::chrono_literals;
    SetUnhandledExceptionFilter(CrashdumpFilter);

    if (int config_rc; config_rc = Config::LoadConfig(argc, argv) >= 0) {
        return config_rc;
    }

    Log::init_stdout_logging(LogOptions{Config::EnableTracing});

    ActorEnvironment env;
    google::protobuf::Any any_msg;
    std::string socket_type;

    if (Config::HolepuncherIP.empty()) {
        fp_actor::SocketInitDirect socket_init;
        socket_init.set_port(Config::Port);
        if (!Config::IsHost) {
            LOG_INFO("Initializing watching for direct connection to {}:{}", Config::ServerIP, Config::Port);
            socket_init.set_ip(Config::ServerIP);
            socket_init.set_name(Config::Identifier);
            socket_type = "ClientSocketActor";
        } else {
            LOG_INFO("Initializing hosting for direct connection");
            socket_type = "HostSocketActor";
        }
        any_msg.PackFrom(socket_init);
    } else {
        fp_actor::SocketInitHolepunch socket_init;
        socket_init.set_hp_ip(Config::HolepuncherIP);
        socket_init.set_port(Config::Port);
        socket_init.set_name(Config::Identifier);
        if (!Config::IsHost) {
            LOG_INFO("Initializing watching host {} under name {} with puncher={}:{}", Config::HostIdentifier, Config::Identifier, Config::HolepuncherIP, Config::Port);
            socket_init.set_target_name(Config::HostIdentifier);
            socket_type = "ClientSocketActor";
        } else {
            LOG_INFO("Initializing hosting under name {} with puncher={}:{}", Config::Identifier, Config::HolepuncherIP, Config::Port);
            socket_type = "HostSocketActor";
        }
        any_msg.PackFrom(socket_init);
    }
    env.AddActor(socket_type, SOCKET_ACTOR_NAME, std::make_optional(std::move(any_msg)));

    if (Config::IsHost) {
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