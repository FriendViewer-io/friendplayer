#include "decoder/FramePresenterGLUT.h"

#include <GL/glut.h>
#include <GL/freeglut_ext.h>
#include <cudaGL.h>
#include <sstream>

#include "common/Log.h"
#include "actors/HostActor.h"

static FramePresenterGLUT* pInstance;

CUdeviceptr FramePresenterGLUT::RegisterContext(CUcontext context, int width, int height, int stream_num) {
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

void FramePresenterGLUT::DisplayProc() {
    if (!pInstance) {
        return;
    }
    pInstance->Render(pInstance->presenters[glutGetWindow()]);
}

void FramePresenterGLUT::CloseWindowProc() {
    if (!pInstance) {
        return;
    }
    // Enable the flag to break glutMainLoopEvent
    pInstance->stop = true;
}

bool FramePresenterGLUT::TranslateCoords(PresenterInfo& info, int& x, int& y) {
    if (x >= info.display_rect.left && x <= info.display_rect.right
        && y >= info.display_rect.top && y < info.display_rect.bottom) {
        x = static_cast<int>((x - info.display_rect.left) * (info.width / static_cast<float>(info.display_rect.right - info.display_rect.left)));
        y = static_cast<int>((y - info.display_rect.top) * (info.height / static_cast<float>(info.display_rect.bottom - info.display_rect.top)));
        return true;
    }

    return false;
}

void FramePresenterGLUT::KeyProc(unsigned char key, int x, int y) {
    if (!pInstance) {
        return;
    }
    if (glutGetModifiers() & GLUT_ACTIVE_ALT && key == '\r') {
        glutFullScreenToggle();
    }
    int virtual_key = VkKeyScan(key) & 0xFF;
    if (!pInstance->key_press_map[virtual_key]) {
        pInstance->key_press_map[virtual_key] = 1;
        pInstance->callback_inst->OnKeyPress(virtual_key, true);
        LOG_INFO("Key down: {}", key);
    }
}

int GLUTtoVirtualKey(int glut_key) {
    
    switch (glut_key) {
    case GLUT_KEY_F1:
    case GLUT_KEY_F2:
    case GLUT_KEY_F3:
    case GLUT_KEY_F4:
    case GLUT_KEY_F5:
    case GLUT_KEY_F6:
    case GLUT_KEY_F7:
    case GLUT_KEY_F8:
    case GLUT_KEY_F9:
    case GLUT_KEY_F10:
    case GLUT_KEY_F11:
    case GLUT_KEY_F12:
        return VK_F1 + glut_key - GLUT_KEY_F1;
    case GLUT_KEY_LEFT:
    case GLUT_KEY_RIGHT:
    case GLUT_KEY_UP:
    case GLUT_KEY_DOWN:
        return VK_LEFT + glut_key - GLUT_KEY_LEFT;
    case GLUT_KEY_PAGE_UP:
    case GLUT_KEY_PAGE_DOWN:
    case GLUT_KEY_HOME:
    case GLUT_KEY_END:
        return VK_PRIOR + glut_key - GLUT_KEY_PAGE_UP;
    case GLUT_KEY_INSERT:
        return VK_INSERT;
    }
    
    return 0;
}

void FramePresenterGLUT::KeyUpProc(unsigned char key, int x, int y) {
    if (!pInstance) {
        return;
    }
    int virtual_key = VkKeyScan(key) & 0xFF;
    if (pInstance->key_press_map[virtual_key]) {
        pInstance->key_press_map[virtual_key] = 0;
        // THis shit should be fixed :) smiley-face
        pInstance->callback_inst->OnKeyPress(virtual_key, false);
        LOG_INFO("Key up: {}", key);
    }
}

void FramePresenterGLUT::SpecialKeyProc(int key, int x, int y) {
    if (!pInstance) {
        return;
    }
    int virtual_key = GLUTtoVirtualKey(key);
    if (!pInstance->key_press_map[virtual_key]) {
        pInstance->key_press_map[virtual_key] = 0;
        // THis shit should be fixed :) smiley-face
        pInstance->callback_inst->OnKeyPress(virtual_key, true);
        LOG_INFO("Key up: {}", key);
    }
}

void FramePresenterGLUT::SpecialKeyUpProc(int key, int x, int y) {
    if (!pInstance) {
        return;
    }
    int virtual_key = GLUTtoVirtualKey(key);
    if (pInstance->key_press_map[virtual_key]) {
        pInstance->key_press_map[virtual_key] = 0;
        // THis shit should be fixed :) smiley-face
        pInstance->callback_inst->OnKeyPress(virtual_key, false);
        LOG_INFO("Key down: {}", key);
    }
}

void FramePresenterGLUT::MouseButtonProc(int button, int state, int x, int y) {
    if (!pInstance) {
        return;
    }
    if (!pInstance->mouse_press_map[button] && state == GLUT_DOWN
        || pInstance->mouse_press_map[button] && state == GLUT_UP) {
        PresenterInfo& info = pInstance->presenters[glutGetWindow()];
        if (pInstance->TranslateCoords(info, x, y)) {
            pInstance->mouse_press_map[button] = !pInstance->mouse_press_map[button];

            pInstance->callback_inst->OnMousePress(info.stream_num, x, y, button, state == GLUT_DOWN);
            LOG_INFO("Mouse click: {} {} {}", x, y, button);
        }
    }
}

void FramePresenterGLUT::MouseMotionProc(int x, int y) {
    if (!pInstance) {
        return;
    }
    PresenterInfo& info = pInstance->presenters[glutGetWindow()];
    if (pInstance->TranslateCoords(info, x, y)) {
        pInstance->callback_inst->OnMouseMove(info.stream_num, x, y);
        LOG_INFO("Mouse motion: {} {}", x, y);
    }
}

void FramePresenterGLUT::Run(int num_presenters) {
    int argc = 1;
    const char* argv[] = { "dummy" };
    glutInit(&argc, (char**)argv);

    while (presenters.size() < num_presenters) {
        PresenterInfo new_info;
        new_presenter_queue.wait_dequeue(new_info);
        LOG_INFO("Got new presenter {}x{}", new_info.width, new_info.height);
        float aspect_ratio = static_cast<float>(new_info.width) / new_info.height;
        int width = static_cast<int>(GetSystemMetrics(SM_CXSCREEN) * 0.75f);
        int height = static_cast<int>(new_info.width / aspect_ratio);
        glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE);
        glutInitWindowSize(width, height);

        new_info.window_id = glutCreateWindow("FramePresenterGLUT");
        glutSetOption(GLUT_ACTION_ON_WINDOW_CLOSE, GLUT_ACTION_CONTINUE_EXECUTION);
        glutSetCursor(GLUT_CURSOR_NONE);

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

        // Register display function
        glutDisplayFunc(DisplayProc);
        // Register window close event callback function
        glutCloseFunc(CloseWindowProc);

        glutKeyboardFunc(KeyProc);
        glutKeyboardUpFunc(KeyUpProc);
        glutMouseFunc(MouseButtonProc);
        glutPassiveMotionFunc(MouseMotionProc);
        glutSpecialFunc(SpecialKeyProc);
        glutSpecialUpFunc(SpecialKeyUpProc);

        presenters.emplace(new_info.window_id, new_info);
    }

    LOG_INFO("All presenters registered, beginning render");
    pInstance = this;
    // Launch the rendering loop
    while (!stop) {
        glutMainLoopEvent();
    }
    pInstance = NULL;

    for (auto& [id, info] : presenters) {
        cuMemFree(info.frame);
        glDeleteBuffersARB(1, &info.pbo);
        glDeleteTextures(1, &info.tex);
        glDeleteProgramsARB(1, &info.shader);
    }
}

/**
*   @brief  Rendering function called by glut
*   @return void
*/
void FramePresenterGLUT::Render(PresenterInfo& info) {
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

    // Bind OpenGL buffer object and upload the data
    glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, info.pbo);
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB, info.tex);
    glTexSubImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, info.width, info.height, GL_BGRA, GL_UNSIGNED_BYTE, 0);
    glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 0);

    glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, info.shader);
    glEnable(GL_FRAGMENT_PROGRAM_ARB);
    glDisable(GL_DEPTH_TEST);

    int window_width = glutGet(GLUT_WINDOW_WIDTH);
    int window_height = glutGet(GLUT_WINDOW_HEIGHT);

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

    glutSwapBuffers();
    glutPostRedisplay();
}

void FramePresenterGLUT::PrintText(int iFont, std::string strText, int x, int y, bool bFillBackground) {
    struct { void* font; int d1; int d2; } fontData[] = {
        /*0*/ GLUT_BITMAP_9_BY_15,        13, 4,
        /*1*/ GLUT_BITMAP_8_BY_13,        11, 4,
        /*2*/ GLUT_BITMAP_TIMES_ROMAN_10, 9,  3,
        /*3*/ GLUT_BITMAP_TIMES_ROMAN_24, 20, 7,
        /*4*/ GLUT_BITMAP_HELVETICA_10,   10, 3,
        /*5*/ GLUT_BITMAP_HELVETICA_12,   11, 4,
        /*6*/ GLUT_BITMAP_HELVETICA_18,   16, 5,
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

    glPopMatrix();
}
