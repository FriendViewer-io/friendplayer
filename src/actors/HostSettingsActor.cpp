#include "actors/HostSettingsActor.h"

#include "actors/CommonActorNames.h"
#include "common/Log.h"

#include <asio/ip/udp.hpp>

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <imgui_internal.h>

static HostSettingsActor* pInstance;

HostSettingsActor::HostSettingsActor(const ActorMap& actor_map, DataBufferMap& buffer_map, std::string&& name)
    : Actor(actor_map, buffer_map, std::move(name))  {}

HostSettingsActor::~HostSettingsActor() {}

void HostSettingsActor::OnMessage(const any_msg& msg) {
    if (msg.Is<fp_actor::AddClientSettings>()) {
        fp_actor::AddClientSettings add_msg;
        msg.UnpackTo(&add_msg);
        OnClientAdded(add_msg);
    } else if (msg.Is<fp_actor::RemoveClientSettings>()) {
        fp_actor::RemoveClientSettings remove_msg;
        msg.UnpackTo(&remove_msg);
        OnClientRemoved(remove_msg);
    } else if (msg.Is<fp_actor::UpdateClientSetting>()) {
        fp_actor::UpdateClientSetting update_msg;
        msg.UnpackTo(&update_msg);
        OnClientUpdated(update_msg);
    } else {
        Actor::OnMessage(msg);
    }

}

void HostSettingsActor::OnInit(const std::optional<any_msg>& init_msg) {
    present_thread = std::make_unique<std::thread>(&HostSettingsActor::Present, this);
}

void HostSettingsActor::OnFinish() {
    stop = true;
    present_thread->join();
    Actor::OnFinish();
}

void HostSettingsActor::OnClientAdded(const fp_actor::AddClientSettings& msg) {
    Client client;
    client.actor_name = msg.actor_name();
    client.ip = asio::ip::address_v4(msg.address() & 0xFFFFFFFF).to_string();
    client.port = static_cast<short>((msg.address() >> 32) & 0xFFFF);
    client.is_controller_connected = false;
    client.is_keyboard_enabled = false;
    client.is_mouse_enabled = false;
    client.is_controller_enabled = true;
    connected_clients[msg.actor_name()] = client;
    UpdateClientActorState(client);
}

void HostSettingsActor::OnClientRemoved(const fp_actor::RemoveClientSettings& msg) {
    connected_clients.erase(msg.actor_name());
}

void HostSettingsActor::OnClientUpdated(const fp_actor::UpdateClientSetting& msg) {
    const auto& it = connected_clients.find(msg.actor_name());
    if (it == connected_clients.end()) {
        LOG_ERROR("Tried to update client state of unknown client {}", msg.actor_name());
        return;
    }
    if (msg.has_has_controller()) {
        it->second.is_controller_connected = msg.has_controller();
    }
    if (msg.has_ping()) {
        it->second.ping = msg.ping();
    }
    if (msg.has_client_name()) {
        it->second.client_name = msg.client_name();
    }
    if (msg.has_finished_handshake()) {
        it->second.is_ready = msg.finished_handshake();
    }
}

void HostSettingsActor::UpdateClientActorState(const Client& client) {
    fp_actor::ChangeClientActorState change_msg;
    change_msg.set_controller_enabled(client.is_controller_enabled);
    change_msg.set_keyboard_enabled(client.is_keyboard_enabled);
    change_msg.set_mouse_enabled(client.is_mouse_enabled);
    SendTo(client.actor_name, change_msg);
}

void HostSettingsActor::WindowCloseProc(GLFWwindow* window) {
    if (!pInstance) {
        return;
    }
    fp_actor::Kill kill_msg;
    pInstance->SendTo(CLIENT_MANAGER_ACTOR_NAME, kill_msg);
}

void HostSettingsActor::Present() {
    glfwInit();
    ImGuiContext* ctx = nullptr;
    int width = 300;
    int height = 255;
    //glfwWindowHint(GLFW_AUTO_ICONIFY, GL_FALSE);
    glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);
    GLFWwindow* window = glfwCreateWindow(width, height, "Host Settings", NULL, NULL);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(0);

    glfwSetWindowCloseCallback(window, WindowCloseProc);

    ctx = ImGui::CreateContext();
    IMGUI_CHECKVERSION();
    ImGui_ImplOpenGL3_Init();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui::StyleColorsDark();
    auto& io = ImGui::GetIO();
    io.IniFilename = NULL;
    io.Fonts->AddFontDefault();
    glewInit();

    pInstance = this;
    while (!stop) {

        int window_width;
        int window_height;
        glfwGetWindowSize(window, &window_width, &window_height);

        glViewport(0, 0, window_width, window_height);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0.0, 1.0, 0.0, 1.0, 0.0, 1.0);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowSize(ImVec2(window_width, window_height));
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::Begin("Options", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
        static bool mute_state = false;

        {
            std::lock_guard<std::mutex> lock(clients_mutex);
            bool first_client = true;
            if (connected_clients.size() == 0) {
                ImGui::Text("No clients connected");
            }
            for (auto&& [actor_name, client] : connected_clients) {
                if (!first_client) {
                    ImGui::Separator();
                }
                first_client = false;
                ImGui::Text(fmt::format("{} {}:{}", !client.is_ready ? "Client" : client.client_name, client.ip, client.port).c_str());
                std::string ping_text = fmt::format("Ping: {}ms", client.ping);
                ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize(ping_text.c_str()).x - ImGui::GetWindowScrollbarRect(ImGui::GetCurrentWindow(), ImGuiAxis_Y).GetWidth() - 5);
                ImGui::Text(ping_text.c_str());
                if (!client.is_ready) {
                    ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
                    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
                }
                if (ImGui::Checkbox(fmt::format("Mouse##{}", client.actor_name).c_str(), &client.is_mouse_enabled)) {
                    UpdateClientActorState(client);
                }
                if (ImGui::Checkbox(fmt::format("Keyboard##{}", client.actor_name).c_str(), &client.is_keyboard_enabled)) {
                    UpdateClientActorState(client);
                }
                if (!client.is_controller_connected) {
                    ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
                    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
                }
                if (ImGui::Checkbox(fmt::format("Controller##{}", client.actor_name).c_str(), &client.is_controller_enabled)) {
                    UpdateClientActorState(client);
                }
                if (!client.is_controller_connected) {
                    ImGui::PopItemFlag();
                    ImGui::PopStyleVar();
                }
                if (!client.is_ready) {
                    ImGui::PopItemFlag();
                    ImGui::PopStyleVar();
                }
                if (ImGui::Button(fmt::format("Kick##{}", client.actor_name).c_str())) {
                    fp_actor::ClientKick kick_msg;
                    SendTo(client.actor_name, kick_msg);
                }
            }
        }
        ImGui::End();
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
        glfwPollEvents();
        glFlush();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    LOG_INFO("FramePresenter exiting");

    pInstance = NULL;
}