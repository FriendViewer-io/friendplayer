#include "decoder/FramePresenterGL.h"

#include <cudaGL.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <sstream>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include "common/Log.h"
#include "actors/HostActor.h"

static FramePresenterGL* pInstance;

#undef min
#undef max

GLFWmonitor* GetBestMonitor(GLFWwindow* window) {
    int overlap, best_overlap;
    best_overlap = 0;
    GLFWmonitor* best_monitor = glfwGetPrimaryMonitor();

    int wx, wy, ww, wh;
    glfwGetWindowPos(window, &wx, &wy);
    glfwGetWindowSize(window, &ww, &wh);
    int num;
    GLFWmonitor** monitors = glfwGetMonitors(&num);

    for (int i = 0; i < num; ++i) {
        GLFWmonitor* monitor = monitors[i];
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        int mx, my;
        glfwGetMonitorPos(monitor, &mx, &my);

        overlap =
            std::max(0, std::min(wx + ww, mx + mode->width) - std::max(wx, mx)) *
            std::max(0, std::min(wy + wh, my + mode->height) - std::max(wy, my));

        if (best_overlap < overlap) {
            best_overlap = overlap;
            best_monitor = monitor;
        }
    }

    return best_monitor;
}

CUdeviceptr FramePresenterGL::RegisterContext(CUcontext context, int width, int height, int stream_num) {
    CUdeviceptr device_frame = NULL;
    cuMemAlloc(&device_frame, width * height * 4);
    cuMemsetD8(device_frame, 0, width * height * 4);
    PresenterInfo new_info;
    new_info.context = context;
    new_info.frame = device_frame;
    new_info.width = width;
    new_info.height = height;
    new_info.text = "Windoww";
    new_info.stream_num = stream_num;
    new_info.fullscreen_state = false;
    new_presenter_queue.enqueue(new_info);
    return device_frame;
}

bool FramePresenterGL::TranslateCoords(PresenterInfo& info, double& x, double& y) {
    if (x >= info.display_rect.left && x <= info.display_rect.right
        && y >= info.display_rect.top && y < info.display_rect.bottom) {
        x = static_cast<int>((x - info.display_rect.left) * (info.width / static_cast<float>(info.display_rect.right - info.display_rect.left)));
        y = static_cast<int>((y - info.display_rect.top) * (info.height / static_cast<float>(info.display_rect.bottom - info.display_rect.top)));
        return true;
    }

    return false;
}

int ConvertToVK(int glfw_key) {
    if (glfw_key == GLFW_KEY_SPACE) {
        return VK_SPACE;
    } else if (glfw_key == GLFW_KEY_APOSTROPHE) {
        return VK_OEM_7;
    } else if (glfw_key == GLFW_KEY_COMMA) {
        return VK_OEM_COMMA;
    } else if (glfw_key == GLFW_KEY_MINUS) {
        return VK_OEM_MINUS;
    } else if (glfw_key == GLFW_KEY_PERIOD) {
        return VK_OEM_PERIOD;
    } else if (glfw_key == GLFW_KEY_SLASH) {
        return VK_OEM_2;
    } else if (glfw_key >= GLFW_KEY_0 && glfw_key <= GLFW_KEY_9) {
        return 0x30 + glfw_key - GLFW_KEY_0;
    } else if (glfw_key == GLFW_KEY_SEMICOLON) {
        return VK_OEM_1;
    } else if (glfw_key == GLFW_KEY_EQUAL) {
        return VK_OEM_PLUS;
    } else if (glfw_key >= GLFW_KEY_A && glfw_key <= GLFW_KEY_Z) {
        return 0x41 + glfw_key - GLFW_KEY_A;
    } else if (glfw_key == GLFW_KEY_LEFT_BRACKET) {
        return VK_OEM_4;
    } else if (glfw_key == GLFW_KEY_BACKSLASH) {
        return VK_OEM_5;
    } else if (glfw_key == GLFW_KEY_RIGHT_BRACKET) {
        return VK_OEM_6;
    } else if (glfw_key == GLFW_KEY_GRAVE_ACCENT) {
        return VK_OEM_3;
    } else if (glfw_key == GLFW_KEY_ESCAPE) {
        return VK_ESCAPE;
    } else if (glfw_key == GLFW_KEY_ENTER) {
        return VK_RETURN;
    } else if (glfw_key == GLFW_KEY_TAB) {
        return VK_TAB;
    } else if (glfw_key == GLFW_KEY_BACKSPACE) {
        return VK_BACK;
    } else if (glfw_key == GLFW_KEY_INSERT) {
        return VK_INSERT;
    } else if (glfw_key == GLFW_KEY_DELETE) {
        return VK_DELETE;
    } else if (glfw_key == GLFW_KEY_RIGHT) {
        return VK_RIGHT;
    } else if (glfw_key == GLFW_KEY_LEFT) {
        return VK_LEFT;
    } else if (glfw_key == GLFW_KEY_DOWN) {
        return VK_DOWN;
    } else if (glfw_key == GLFW_KEY_UP) {
        return VK_UP;
    } else if (glfw_key == GLFW_KEY_PAGE_UP) {
        return VK_PRIOR;
    } else if (glfw_key == GLFW_KEY_PAGE_DOWN) {
        return VK_NEXT;
    } else if (glfw_key == GLFW_KEY_HOME) {
        return VK_HOME;
    } else if (glfw_key == GLFW_KEY_END) {
        return VK_END;
    } else if (glfw_key == GLFW_KEY_CAPS_LOCK) {
        return VK_CAPITAL;
    } else if (glfw_key == GLFW_KEY_SCROLL_LOCK) {
        return VK_SCROLL;
    } else if (glfw_key == GLFW_KEY_NUM_LOCK) {
        return VK_NUMLOCK;
    } else if (glfw_key == GLFW_KEY_PRINT_SCREEN) {
        return VK_SNAPSHOT;
    } else if (glfw_key == GLFW_KEY_PAUSE) {
        return VK_PAUSE;
    } else if (glfw_key >= GLFW_KEY_F1 && glfw_key <= GLFW_KEY_F24) {
        return VK_F1 + glfw_key - GLFW_KEY_F1;
    } else if (glfw_key >= GLFW_KEY_KP_0 && glfw_key <= GLFW_KEY_KP_9) {
        return VK_NUMPAD0 + glfw_key - GLFW_KEY_KP_0;
    } else if (glfw_key == GLFW_KEY_KP_DECIMAL) {
        return VK_DECIMAL;
    } else if (glfw_key == GLFW_KEY_KP_DIVIDE) {
        return VK_DIVIDE;
    } else if (glfw_key == GLFW_KEY_KP_SUBTRACT) {
        return VK_SUBTRACT;
    } else if (glfw_key == GLFW_KEY_KP_ADD) {
        return VK_ADD;
    } else if (glfw_key == GLFW_KEY_KP_ENTER) {
        return VK_RETURN;
    } else if (glfw_key == GLFW_KEY_LEFT_SHIFT) {
        return VK_LSHIFT;
    } else if (glfw_key == GLFW_KEY_LEFT_CONTROL) {
        return VK_LCONTROL;
    } else if (glfw_key == GLFW_KEY_LEFT_ALT) {
        return VK_MENU;
    } else if (glfw_key == GLFW_KEY_RIGHT_SHIFT) {
        return VK_RSHIFT;
    } else if (glfw_key == GLFW_KEY_RIGHT_CONTROL) {
        return VK_RCONTROL;
    } else if (glfw_key == GLFW_KEY_RIGHT_ALT) {
        return VK_MENU;
    }

    return 0;
}

void FramePresenterGL::KeyProc(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (!pInstance || ImGui::GetIO().WantCaptureKeyboard) {
        return;
    }
    int virtual_key = ConvertToVK(key);
    if (action == GLFW_RELEASE) {
        pInstance->key_press_map[virtual_key] = 0;
    } else {
        pInstance->key_press_map[virtual_key] = 1;
    }
    pInstance->callback_inst->OnKeyPress(virtual_key, action != GLFW_RELEASE);
}

void FramePresenterGL::MouseButtonProc(GLFWwindow* window, int button, int action, int mods) {
    if (!pInstance || ImGui::GetIO().WantCaptureMouse) {
        return;
    }
    
    if (action != GLFW_REPEAT) {
        PresenterInfo& info = pInstance->presenters[window];
        double x, y;
        glfwGetCursorPos(window, &x, &y);
        if (pInstance->TranslateCoords(info, x, y)) {
            pInstance->mouse_press_map[button].flip();
            int button_proto = 0;
            if (button == GLFW_MOUSE_BUTTON_LEFT) {
                button_proto = fp_network::MouseFrame::MOUSE_L;
            } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
                button_proto = fp_network::MouseFrame::MOUSE_R;
            } else if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
                button_proto = fp_network::MouseFrame::MOUSE_MIDDLE;
            } else if (button == GLFW_MOUSE_BUTTON_4) {
                button_proto = fp_network::MouseFrame::MOUSE_X1;
            } else if (button == GLFW_MOUSE_BUTTON_5) {
                button_proto = fp_network::MouseFrame::MOUSE_X2;
            }
            pInstance->callback_inst->OnMousePress(info.stream_num, x, y, button_proto, action == GLFW_PRESS);
        }
    }
}

void FramePresenterGL::MousePosProc(GLFWwindow* window, double x, double y) {
    if (!pInstance || ImGui::GetIO().WantCaptureMouse) {
        return;
    }
    
    PresenterInfo& info = pInstance->presenters[window];
    if (pInstance->TranslateCoords(info, x, y) && glfwGetWindowAttrib(window, GLFW_FOCUSED)) {
        pInstance->callback_inst->OnMouseMove(info.stream_num, x, y);
    }
}

void FramePresenterGL::WindowCloseProc(GLFWwindow* window) {
    if (!pInstance) {
        return;
    }
    pInstance->callback_inst->OnWindowClosed();
}

void FramePresenterGL::MouseWheelProc(GLFWwindow* window, double x_offset, double y_offset) {
    if (!pInstance || ImGui::GetIO().WantCaptureMouse) {
        return;
    }
    if (glfwGetWindowAttrib(window, GLFW_FOCUSED)) {
        double x, y;
        glfwGetCursorPos(window, &x, &y);
        PresenterInfo& info = pInstance->presenters[window];
        if (pInstance->TranslateCoords(info, x, y)) {
            pInstance->callback_inst->OnMouseScroll(info.stream_num, x, y, x_offset, y_offset);
        }
    }
}

void FramePresenterGL::Run(int num_presenters) {
    glfwInit();

    ImGuiContext* ctx = nullptr;

    while (presenters.size() < num_presenters) {
        PresenterInfo new_info;
        new_presenter_queue.wait_dequeue(new_info);
        LOG_INFO("Got new presenter {}x{} = {}", new_info.width, new_info.height, new_info.stream_num);
        float aspect_ratio = static_cast<float>(new_info.width) / new_info.height;
        int width = static_cast<int>(GetSystemMetrics(SM_CXSCREEN) * 0.75f);
        int height = static_cast<int>(new_info.width / aspect_ratio);
        glfwWindowHint(GLFW_AUTO_ICONIFY, GL_FALSE);
        new_info.window = glfwCreateWindow(width, height, fmt::format("Monitor Stream {}", new_info.stream_num).c_str(), NULL, main_window);

        glfwMakeContextCurrent(new_info.window);

        glfwSwapInterval(0);

        glfwSetMouseButtonCallback(new_info.window, MouseButtonProc);
        glfwSetCursorPosCallback(new_info.window, MousePosProc);
        glfwSetKeyCallback(new_info.window, KeyProc);
        glfwSetWindowCloseCallback(new_info.window, WindowCloseProc);
        glfwSetScrollCallback(new_info.window, MouseWheelProc);

        if (ctx == nullptr) {
            ctx = ImGui::CreateContext();
            IMGUI_CHECKVERSION();
            ImGui_ImplOpenGL3_Init();
            ImGui_ImplGlfw_InitForOpenGL(new_info.window, true);
            ImGui::StyleColorsDark();
            auto& io = ImGui::GetIO();
            io.IniFilename = NULL;
            io.Fonts->AddFontDefault();
            main_window = new_info.window;
        }

        glewInit();

        // Prepare OpenGL buffer object for uploading texture data
        glGenBuffersARB(1, &new_info.pbo);
        glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, new_info.pbo);
        glBufferDataARB(GL_PIXEL_UNPACK_BUFFER_ARB, new_info.width * new_info.height * 4, NULL, GL_STREAM_DRAW_ARB);
        glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 0);

        // Create OpenGL texture and upload frame data
        glGenTextures(1, &new_info.tex);
        glBindTexture(GL_TEXTURE_RECTANGLE_ARB, new_info.tex);
        glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8, new_info.width, new_info.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_RECTANGLE_ARB, 0);

        // Create and initialize fragment program
        static const char* code =
            "!!ARBfp1.0\n"
            "TEX result.color, fragment.texcoord, texture[0], RECT; \n"
            "END";
        glGenProgramsARB(1, &new_info.shader);
        glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, new_info.shader);
        glProgramStringARB(GL_FRAGMENT_PROGRAM_ARB, GL_PROGRAM_FORMAT_ASCII_ARB, (GLsizei)strlen(code), (GLubyte*)code);

        cuCtxPushCurrent(new_info.context);
        cuGraphicsGLRegisterBuffer(&new_info.cuResource, new_info.pbo, CU_GRAPHICS_REGISTER_FLAGS_WRITE_DISCARD);
        cuGraphicsMapResources(1, &new_info.cuResource, 0);
        cuCtxPopCurrent(NULL);

        presenters.emplace(new_info.window, new_info);
    }

    LOG_INFO("All presenters registered, beginning render");
    pInstance = this;

    while (!stop) {
        for (auto& [window, info] : presenters) {
            Render(info);
        }
        glfwPollEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    Cleanup();

    LOG_INFO("FramePresenter exiting");

    pInstance = NULL;
}

void FramePresenterGL::Stop() {
    if (!stop) {
        stop = true;
        message_loop->join();
    }
}

void FramePresenterGL::Cleanup() {
    for (auto& [id, info] : presenters) {    
        cuGraphicsUnmapResources(1, &info.cuResource, 0);
        cuGraphicsUnregisterResource(info.cuResource);
        glDeleteBuffersARB(1, &info.pbo);
        glDeleteTextures(1, &info.tex);
        glDeleteProgramsARB(1, &info.shader);
    }
    presenters.clear();
}

/**
*   @brief  Rendering function called by glut
*   @return void
*/
void FramePresenterGL::Render(PresenterInfo& info) {
    // Register the OpenGL buffer object with CUDA and map a CUdeviceptr onto it
    // The decoder code will receive this CUdeviceptr and copy the decoded frame into the associated device memory allocation
    cuCtxPushCurrent(info.context);
    CUdeviceptr dpBackBuffer;
    size_t nSize = 0;
    cuGraphicsResourceGetMappedPointer(&dpBackBuffer, &nSize, info.cuResource);

    CUDA_MEMCPY2D m = { 0 };
    m.srcMemoryType = CU_MEMORYTYPE_DEVICE;

    // Source is dpFrame into which Decode() function writes data of individual frame present in BGRA32 format
    // Destination is OpenGL buffer object mapped as a CUDA resource
    m.srcDevice = info.frame;
    m.srcPitch = info.width * 4;
    m.dstMemoryType = CU_MEMORYTYPE_DEVICE;
    m.dstDevice = dpBackBuffer;
    m.dstPitch = nSize / info.height;
    m.WidthInBytes = info.width * 4;
    m.Height = info.height;
    cuMemcpy2DAsync(&m, 0);

    cuCtxPopCurrent(NULL);

    glfwMakeContextCurrent(info.window);

    // Bind OpenGL buffer object and upload the data
    glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, info.pbo);
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB, info.tex);
    glTexSubImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, info.width, info.height, GL_BGRA, GL_UNSIGNED_BYTE, 0);
    glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 0);

    glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, info.shader);
    glEnable(GL_FRAGMENT_PROGRAM_ARB);
    glDisable(GL_DEPTH_TEST);

    int window_width;
    int window_height;
    glfwGetWindowSize(info.window, &window_width, &window_height);

    float new_ratio = (window_width) / static_cast<float>(window_height);
    float source_ratio = info.width / (float)info.height;
    int inner_window_width, inner_window_height;

    if (new_ratio > source_ratio) {
        inner_window_height = window_height;
        inner_window_width = static_cast<int>(window_height * source_ratio);
    }
    else {
        inner_window_width = window_width;
        inner_window_height = static_cast<int>(window_width / source_ratio);
    }
    
    glViewport(0, 0, window_width, window_height);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, 1.0, 0.0, 1.0, 0.0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);

    info.display_rect.left = (window_width / 2) - (inner_window_width / 2);
    info.display_rect.top = (window_height / 2) - (inner_window_height / 2);
    info.display_rect.right = info.display_rect.left + inner_window_width;
    info.display_rect.bottom = info.display_rect.top + inner_window_height;

    float left, right, top, bottom;

    left = info.display_rect.left / static_cast<float>(window_width);
    right = info.display_rect.right / static_cast<float>(window_width);
    top = info.display_rect.top / static_cast<float>(window_height);
    bottom = info.display_rect.bottom / static_cast<float>(window_height);

    // Specify vertex and texture co-ordinates
    glBegin(GL_QUADS);
    glTexCoord2f(0, (GLfloat)info.height);
    glVertex2f(left, top);
    glTexCoord2f((GLfloat)info.width, (GLfloat)info.height);
    glVertex2f(right, top);
    glTexCoord2f((GLfloat)info.width, 0);
    glVertex2f(right, bottom);
    glTexCoord2f(0, 0);
    glVertex2f(left, bottom);
    glEnd();
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB, 0);
    glDisable(GL_FRAGMENT_PROGRAM_ARB);
    
    if (main_window == info.window) {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowSize(ImVec2(0, 0));

        ImGui::Begin("Options", nullptr, ImGuiWindowFlags_NoResize);

        static int volume = 100;
        if (ImGui::SliderInt("Volume", &volume, 0, 100, "%d")) {
            callback_inst->VolumeState(volume / 100.0f);
        }
        static bool mute_state = false;
        if (ImGui::Checkbox("Mute Sound", &mute_state)) {
            callback_inst->MuteState(mute_state);
        }
        for (auto& [id, draw_info] : presenters) {
            if (ImGui::Checkbox(fmt::format("Fullscreen #{}", draw_info.stream_num).c_str(), &draw_info.fullscreen_state)) {
                GLFWmonitor* current_monitor = glfwGetWindowMonitor(draw_info.window);
                GLFWmonitor* monitor = GetBestMonitor(draw_info.window);
                const GLFWvidmode* mode = glfwGetVideoMode(monitor);
                if (current_monitor == nullptr) {
                    glfwGetWindowPos(draw_info.window, &draw_info.window_x, &draw_info.window_y);
                    glfwGetWindowSize(draw_info.window, &draw_info.window_width, &draw_info.window_height);
                    glfwSetWindowMonitor(draw_info.window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
                    HWND native_hwnd = glfwGetWin32Window(draw_info.window);
                    RECT rc;
                    GetWindowRect(native_hwnd, &rc);
                    SetWindowPos(native_hwnd, HWND_TOPMOST, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top + 1, SWP_SHOWWINDOW | SWP_FRAMECHANGED | SWP_NOZORDER);
                } else {
                    glfwSetWindowMonitor(draw_info.window, NULL, draw_info.window_x, draw_info.window_y, draw_info.window_width , draw_info.window_height, mode->refreshRate);
                }
            }
        }
        if (ImGui::Button("Disconnect")) {
            callback_inst->OnWindowClosed();
        }
        ImGui::End();
        ImGui::Render();

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glFlush();
    }

    glfwSwapBuffers(info.window);
}

void FramePresenterGL::PrintText(int iFont, std::string strText, int x, int y, bool bFillBackground) {
    
    /*struct { void* font; int d1; int d2; } fontData[] = {
         GLUT_BITMAP_9_BY_15,        13, 4,
         GLUT_BITMAP_8_BY_13,        11, 4,
         GLUT_BITMAP_TIMES_ROMAN_10, 9,  3,
         GLUT_BITMAP_TIMES_ROMAN_24, 20, 7,
         GLUT_BITMAP_HELVETICA_10,   10, 3,
         GLUT_BITMAP_HELVETICA_12,   11, 4,
         GLUT_BITMAP_HELVETICA_18,   16, 5,
    };
    const int nFont = sizeof(fontData) / sizeof(fontData[0]);

    if (iFont >= nFont) {
        iFont = 0;
    }
    void* font = fontData[iFont].font;
    int d1 = fontData[iFont].d1, d2 = fontData[iFont].d2, d = d1 + d2,
        w = glutGet(GLUT_WINDOW_WIDTH), h = glutGet(GLUT_WINDOW_HEIGHT);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, w, 0.0, h, 0.0, 1.0);

    std::stringstream ss(strText);
    std::string str;
    int iLine = 0;
    while (std::getline(ss, str)) {
        glColor3f(1.0, 1.0, 1.0);
        if (bFillBackground) {
            glRasterPos2i(x, h - y - iLine * d - d1);
            for (char c : str) {
                glutBitmapCharacter(font, c);
            }
            GLint pos[4];
            glGetIntegerv(GL_CURRENT_RASTER_POSITION, pos);
            glRecti(x, h - y - iLine * d, pos[0], h - y - (iLine + 1) * d);
            glColor3f(0.0, 0.0, 0.0);
        }
        glRasterPos2i(x, h - y - iLine * d - d1);
        for (char c : str) {
            glutBitmapCharacter(font, c);
        }
        iLine++;
    }

    glPopMatrix();*/
}
