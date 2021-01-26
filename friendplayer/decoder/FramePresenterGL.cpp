#include "decoder/FramePresenterGL.h"

#include <cudaGL.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <sstream>

#include "common/Log.h"
#include "actors/HostActor.h"

static FramePresenterGL* pInstance;

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
    
    return 0;
}

void FramePresenterGL::KeyProc(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (!pInstance) {
        return;
    }
    /*if (glutGetModifiers() & GLUT_ACTIVE_ALT && key == '\r') {
        glutFullScreenToggle();
    }*/
    /*int virtual_key = ConvertToVK(key);
    if (!pInstance->key_press_map[virtual_key]) {
        pInstance->key_press_map[virtual_key] = 1;
        pInstance->callback_inst->OnKeyPress(virtual_key, true);
        LOG_INFO("Key down: {}", key);
    }*/
}

void FramePresenterGL::MouseButtonProc(GLFWwindow* window, int button, int action, int mods) {
    if (!pInstance) {
        return;
    }
    LOG_INFO("Mouse button {} {}", button, action);
    
    if (action != GLFW_REPEAT) {
        PresenterInfo& info = pInstance->presenters[window];
        double x, y;
        glfwGetCursorPos(window, &x, &y);
        if (pInstance->TranslateCoords(info, x, y)) {
            pInstance->mouse_press_map[button].flip();

            pInstance->callback_inst->OnMousePress(info.stream_num, x, y, button, action == GLFW_PRESS);
            LOG_INFO("Mouse click: {} {} {}", x, y, button);
        }
    }
}

void FramePresenterGL::MousePosProc(GLFWwindow* window, double x, double y) {
    if (!pInstance) {
        return;
    }
    
    PresenterInfo& info = pInstance->presenters[window];
    if (pInstance->TranslateCoords(info, x, y) && glfwGetWindowAttrib(window, GLFW_FOCUSED)) {
        pInstance->callback_inst->OnMouseMove(info.stream_num, x, y);
        LOG_INFO("Mouse motion: {} {}", x, y);
    }
}

void FramePresenterGL::OnWindowClose(GLFWwindow* window) {
    if (!pInstance) {
        return;
    }
    pInstance->callback_inst->OnWindowClosed();
}

void FramePresenterGL::Run(int num_presenters) {
    glfwInit();

    while (presenters.size() < num_presenters) {
        PresenterInfo new_info;
        new_presenter_queue.wait_dequeue(new_info);
        LOG_INFO("Got new presenter {}x{}", new_info.width, new_info.height);
        float aspect_ratio = static_cast<float>(new_info.width) / new_info.height;
        int width = static_cast<int>(GetSystemMetrics(SM_CXSCREEN) * 0.75f);
        int height = static_cast<int>(new_info.width / aspect_ratio);
        new_info.window = glfwCreateWindow(width, height, "Simple example", NULL, NULL);

        glfwMakeContextCurrent(new_info.window);
        
        glfwSetMouseButtonCallback(new_info.window, MouseButtonProc);
        glfwSetCursorPosCallback(new_info.window, MousePosProc);
        glfwSetKeyCallback(new_info.window, KeyProc);
        glfwSetWindowCloseCallback(new_info.window, OnWindowClose);

        glViewport(0, 0, width, height);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0.0, 1.0, 0.0, 1.0, 0.0, 1.0);

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

        presenters.emplace(new_info.window, new_info);
    }

    LOG_INFO("All presenters registered, beginning render");
    pInstance = this;

    while (!stop) {
        for (auto& [window, info] : presenters) {
            Render(info);
        }
        glfwPollEvents();
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
    CUgraphicsResource cuResource;
    cuCtxPushCurrent(info.context);
    cuGraphicsGLRegisterBuffer(&cuResource, info.pbo, CU_GRAPHICS_REGISTER_FLAGS_WRITE_DISCARD);
    cuGraphicsMapResources(1, &cuResource, 0);
    CUdeviceptr dpBackBuffer;
    size_t nSize = 0;
    cuGraphicsResourceGetMappedPointer(&dpBackBuffer, &nSize, cuResource);

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

    cuGraphicsUnmapResources(1, &cuResource, 0);
    cuGraphicsUnregisterResource(cuResource);
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
        inner_window_width = window_height * source_ratio;
    }
    else {
        inner_window_width = window_width;
        inner_window_height = window_width / source_ratio;
    }
    
    glViewport(0, 0, window_width, window_height);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, 1.0, 0.0, 1.0, 0.0, 1.0);

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
