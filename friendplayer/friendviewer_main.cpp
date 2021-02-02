#include "actors/ActorEnvironment.h"
#include "actors/CommonActorNames.h"

#include "common/Config.h"
#include "common/Log.h"

#include "protobuf/actor_messages.pb.h"
#include <puncher_messages.pb.h>

#include <chrono>
#include <minidumpapiset.h>

void CreateMiniDump(EXCEPTION_POINTERS* pep) {
    // Open the file
    typedef BOOL(*PDUMPFN)(
        HANDLE hProcess,
        DWORD ProcessId,
        HANDLE hFile,
        MINIDUMP_TYPE DumpType,
        PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam,
        PMINIDUMP_USER_STREAM_INFORMATION UserStreamParam,
        PMINIDUMP_CALLBACK_INFORMATION CallbackParam
        );


    HANDLE hFile = CreateFile(fmt::format("crash_{}.dmp", std::chrono::system_clock::now().time_since_epoch().count()).c_str(), GENERIC_READ | GENERIC_WRITE,
        0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    HMODULE h = ::LoadLibrary("DbgHelp.dll");
    PDUMPFN pFn = (PDUMPFN)GetProcAddress(h, "MiniDumpWriteDump");

    if ((hFile != NULL) && (hFile != INVALID_HANDLE_VALUE)) {
        // Create the minidump

        MINIDUMP_EXCEPTION_INFORMATION mdei;

        mdei.ThreadId = GetCurrentThreadId();
        mdei.ExceptionPointers = pep;
        mdei.ClientPointers = TRUE;
        MINIDUMP_TYPE mdt = MiniDumpNormal;
        BOOL rv = (*pFn)(GetCurrentProcess(), GetCurrentProcessId(),
            hFile, mdt, (pep != 0) ? &mdei : 0, 0, 0);

        // Close the file
        CloseHandle(hFile);
    }
}

LONG WINAPI MyUnhandledExceptionFilter(struct _EXCEPTION_POINTERS* ExceptionInfo) {
    CreateMiniDump(ExceptionInfo);
    return EXCEPTION_EXECUTE_HANDLER;
}

int main(int argc, char** argv) {
    using namespace std::chrono_literals;

    SetUnhandledExceptionFilter(MyUnhandledExceptionFilter);

    if (Config::LoadConfig(argc, argv) >= 0) {
        return 1;
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