#include "actors/SocketActor.h"

#include "actors/CommonActorNames.h"
#include "protobuf/actor_messages.pb.h"
#include "common/Log.h"

void SocketActor::OnInit(const std::optional<any_msg>& init_msg) {
    TimerActor::OnInit(init_msg);
    network_is_running = true;
    network_thread = std::make_unique<std::thread>(&SocketActor::NetworkWorker, this);
}

void SocketActor::OnMessage(const any_msg& msg) {
    if (msg.Is<fp_actor::NetworkSend>()) {
        fp_actor::NetworkSend send_msg;
        msg.UnpackTo(&send_msg);

        fp_network::Network& network_msg = *send_msg.mutable_msg();
        asio_endpoint send_endpoint(asio_address(send_msg.address() & 0xFFFFFFFF), (send_msg.address() >> 32) & 0xFFFF);

        // Fill in buffer with actual data if necessary
        if (network_msg.Payload_case() == fp_network::Network::kDataMsg
            && network_msg.data_msg().Payload_case() == fp_network::Data::kHostFrame) {
            auto& host_frame = *network_msg.mutable_data_msg()->mutable_host_frame();
            uint64_t handle = 0;
            if (host_frame.has_video()) {
                handle = host_frame.video().data_handle();
                host_frame.mutable_video()->clear_DataBacking();
                std::string* buf = buffer_map.GetBuffer(handle);
                host_frame.mutable_video()->set_data(buf->data(), buf->size());
            } else if (host_frame.has_audio()) {
                handle = host_frame.audio().data_handle();
                host_frame.mutable_audio()->clear_DataBacking();
                std::string* buf = buffer_map.GetBuffer(handle);
                host_frame.mutable_audio()->set_data(buf->data(), buf->size());
            }
            buffer_map.Decrement(handle);
        }
        asio::error_code ec;
        socket.send_to(asio::buffer(network_msg.SerializeAsString()), send_endpoint, 0, ec);
    } else {
        TimerActor::OnMessage(msg);
    }
}

void SocketActor::OnFinish() {
    network_is_running = false;
    socket.close();
    network_thread->join();
    TimerActor::OnFinish();
}

void HostSocketActor::OnFinish() {
    if (use_holepunching) {
        fp_puncher::ConnectMessage die_msg;
        die_msg.mutable_exit()->set_host_name(holepunch_identity);
        die_msg.mutable_exit()->set_token(session_token);
        socket.send_to(asio::buffer(die_msg.SerializeAsString()), holepunch_endpoint);
    }
    SocketActor::OnFinish();
}

void SocketActor::NetworkWorker() {
    std::string recv_buffer;
    recv_buffer.resize(1500);

    while (network_is_running) {
        asio::error_code ec;
        asio_endpoint recv_endpoint;
        size_t recv_size = socket.receive_from(asio::buffer(recv_buffer), recv_endpoint, 0, ec);
        if (recv_size == 0 || ec.value() != 0) {
            continue;
        }
        if (use_holepunching && recv_endpoint == holepunch_endpoint) {
            fp_puncher::ServerMessage msg;
            msg.ParseFromArray(recv_buffer.data(), static_cast<int>(recv_size));
            OnPuncherMessage(msg);           
        } else {
            fp_actor::NetworkRecv msg;
            uint64_t address = recv_endpoint.address().to_v4().to_uint();
            address |= static_cast<uint64_t>(recv_endpoint.port()) << 32;
            msg.set_address(address);

            fp_network::Network recv_msg;
            if (!recv_msg.ParseFromArray(recv_buffer.data(), static_cast<int>(recv_size))) {
                continue;
            }
            // Special casing done here, don't kill me
            if (recv_msg.has_hs_msg() &&
                recv_msg.hs_msg().has_phase1()) {
                if (recv_msg.hs_msg().phase1().token() != session_token) {
                    continue;
                }
            }

            // Put data message in buffer
            if (recv_msg.Payload_case() == fp_network::Network::kDataMsg
                && recv_msg.data_msg().Payload_case() == fp_network::Data::kHostFrame) {
                auto& host_frame = *recv_msg.mutable_data_msg()->mutable_host_frame();
                if (host_frame.has_video()) {
                    std::string* data = host_frame.mutable_video()->release_data();
                    host_frame.mutable_video()->clear_DataBacking();
                    host_frame.mutable_video()->set_data_handle(buffer_map.Wrap(data));
                } else if (host_frame.has_audio()) {
                    std::string* data = host_frame.mutable_audio()->release_data();
                    host_frame.mutable_audio()->clear_DataBacking();
                    host_frame.mutable_audio()->set_data_handle(buffer_map.Wrap(data));
                }
            }
            *msg.mutable_msg() = recv_msg;
            SendTo(CLIENT_MANAGER_ACTOR_NAME, msg);
        }
    }
}

void HostSocketActor::OnInit(const std::optional<any_msg>& init_msg) {
    if (init_msg) {
        if (init_msg->Is<fp_actor::SocketInitDirect>()) {
            fp_actor::SocketInitDirect msg;
            init_msg->UnpackTo(&msg);
            socket = asio_socket(io_service, asio_endpoint(asio::ip::udp::v4(), msg.port()));
            use_holepunching = false;
        } else if (init_msg->Is<fp_actor::SocketInitHolepunch>()) {
            fp_actor::SocketInitHolepunch msg;
            init_msg->UnpackTo(&msg);
            holepunch_endpoint = asio_endpoint(asio::ip::address::from_string(msg.hp_ip()), msg.port());
            socket.open(asio::ip::udp::v4());
            holepunch_identity = msg.name();
            use_holepunching = true;

            fp_puncher::ConnectMessage puncher_conn;
            puncher_conn.mutable_host()->set_host_name(msg.name());
            socket.send_to(asio::buffer(puncher_conn.SerializeAsString()), holepunch_endpoint);
            
            SetTimerInternal(5000, true);
        }
        socket.set_option(asio::socket_base::send_buffer_size(CLIENT_SEND_SIZE));
    }
    SocketActor::OnInit(init_msg);
}

void HostSocketActor::OnPuncherMessage(const fp_puncher::ServerMessage& msg) {
    if (msg.has_host()) {
        if (!msg.host().succeeded()) {
            LOG_ERROR("Failed to create host session!");
            fp_actor::Kill kill_msg;
            SendTo(CLIENT_MANAGER_ACTOR_NAME, kill_msg);
            return;
        }
        session_token = msg.host().token();
        LOG_INFO("Got token message from holepuncher: {}", msg.host().token());
    } else if (msg.has_new_client()) {
        asio_endpoint client_ep(asio::ip::address::from_string(msg.new_client().ip()), msg.new_client().port());
    
        for (int i = 0; i < 2; i++) {
            fp_puncher::PunchOpen nil;
            socket.send_to(asio::buffer(nil.SerializeAsString()), client_ep);
        }
    // TODO: hole-punch synchronize here
    } else {
        LOG_CRITICAL("HostSocketActor did not receive correct response from holepuncher");
    }
}

void ClientSocketActor::OnInit(const std::optional<any_msg>& init_msg) {
    if (init_msg) {
        if (init_msg->Is<fp_actor::SocketInitDirect>()) {
            fp_actor::SocketInitDirect msg;
            init_msg->UnpackTo(&msg);
            socket.open(asio::ip::udp::v4());
            use_holepunching = false;

            fp_actor::CreateHostActor create;
            uint64_t host_address = static_cast<uint64_t>(msg.port()) << 32;
            host_address |= asio::ip::address::from_string(msg.ip()).to_v4().to_uint();
            create.set_host_address(host_address);
            create.set_client_identity(msg.name());
            SendTo(CLIENT_MANAGER_ACTOR_NAME, create);
        } else if (init_msg->Is<fp_actor::SocketInitHolepunch>()) {
            fp_actor::SocketInitHolepunch msg;
            init_msg->UnpackTo(&msg);
            holepunch_endpoint = asio_endpoint(asio::ip::address::from_string(msg.hp_ip()), msg.port());
            socket.open(asio::ip::udp::v4());
            holepunch_identity = msg.name();
            use_holepunching = true;

            fp_puncher::ConnectMessage puncher_conn;
            puncher_conn.mutable_client()->set_host_name(msg.target_name());
            socket.send_to(asio::buffer(puncher_conn.SerializeAsString()), holepunch_endpoint);
        }
        socket.set_option(asio::socket_base::receive_buffer_size(CLIENT_RECV_SIZE));
    }
    SocketActor::OnInit(init_msg);
}

void ClientSocketActor::OnPuncherMessage(const fp_puncher::ServerMessage& msg) {
    if (!msg.has_client()) {
        LOG_CRITICAL("ClientSocketActor did not receive ClientResponse from holepuncher");
        return;
    }

    if (!msg.client().succeeded()) {
        LOG_ERROR("Failed to connect to host!");
        fp_actor::Kill kill_msg;
        SendTo(CLIENT_MANAGER_ACTOR_NAME, kill_msg);
        return;
    }

    asio_endpoint host_ep(asio::ip::address::from_string(msg.client().ip()), msg.client().port());
    
    for (int i = 0; i < 2; i++) {
        fp_puncher::PunchOpen nil;
        socket.send_to(asio::buffer(nil.SerializeAsString()), host_ep);
    }
    
    LOG_INFO("Waiting for holepunch to pass");
    // TODO: hole-punch synchronize here, replace sleep
    std::this_thread::sleep_for(std::chrono::seconds(1));

    session_token = msg.client().host_token();
    
    fp_actor::CreateHostActor create;
    create.set_token(msg.client().host_token());

    uint64_t host_address = static_cast<uint64_t>(msg.client().port()) << 32;
    host_address |= host_ep.address().to_v4().to_uint();
    create.set_host_address(host_address);
    create.set_client_identity(holepunch_identity);

    SendTo(CLIENT_MANAGER_ACTOR_NAME, create);
}

void HostSocketActor::OnTimerFire() {
    if (use_holepunching) {
        fp_puncher::ConnectMessage hb;
        hb.mutable_heartbeat()->set_host_name(holepunch_identity);
        hb.mutable_heartbeat()->set_token(session_token);
        socket.send_to(asio::buffer(hb.SerializeAsString()), holepunch_endpoint);
    }
}