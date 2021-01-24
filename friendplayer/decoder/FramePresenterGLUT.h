/*
* Copyright 2017-2020 NVIDIA Corporation.  All rights reserved.
*
* Please refer to the NVIDIA end user license agreement (EULA) associated
* with this source code for terms and conditions that govern your use of
* this software. Any use, reproduction, disclosure, or distribution of
* this software and related documentation outside the terms of the EULA
* is strictly prohibited.
*
*/

#pragma once

#include <iostream>
#include <algorithm>
#include <string.h>
#include <stdlib.h>
#include <mutex>
#include <thread>
#include <chrono>
#include <GL/glew.h>
#include <GL/glut.h>
#include <GL/freeglut_ext.h>
#include <cuda.h>
#include <cudaGL.h>
#include <concurrentqueue/blockingconcurrentqueue.h>

#include "common/NvCodecUtils.h"

class FramePresenterGLUT;
static FramePresenterGLUT *pInstance;

/**
* @brief OpenGL utility class to display decoded frames using OpenGL textures
*/
class FramePresenterGLUT
{
private:
    struct PresenterInfo {
        int width = 0, height = 0, window_id = -1;
        CUdeviceptr frame;
        std::string text;
        GLuint pbo;
        GLuint tex;
        GLuint shader;
        CUcontext context = NULL;
    };
public:
    /**
    *   @brief  FramePresenterGLUT constructor function. This will launch a rendering thread which will be fed by decoded frames
    *   @param  cuContext - CUDA Context handle
    *   @param  nWidth - Width of OpenGL texture
    *   @param  nHeight - Height of OpenGL texture
    */
    FramePresenterGLUT(int num_presenters)
    {
        message_loop = std::make_unique<std::thread>(&FramePresenterGLUT::Run, this, num_presenters);

        // This loop will ensure that OpenGL/glew/glut initialization is finished before we return from this constructor
        /*while (!pInstance) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }*/
    }

    /**
    *   @brief  Destructor function. Also breaks glutMainLoopEvent
    */
    ~FramePresenterGLUT() {
        stop = true;
        message_loop->join();
    }
     
     static CUdeviceptr RegisterContext(CUcontext context, int width, int height) {
         CUdeviceptr device_frame = NULL;
         cuMemAlloc(&device_frame, width * height * 4);
         cuMemsetD8(device_frame, 0, width * height * 4);
         PresenterInfo new_info;
         new_info.context = context;
         new_info.frame = device_frame;
         new_info.width = width;
         new_info.height = height;
         new_info.text = "Windoww";
         new_presenter_queue.enqueue(new_info);
         return device_frame;
     }

private:
     /**
     *   @brief  Registered with glutDisplayFunc() as rendering function
     *   @return void
     */
     static void DisplayProc() {
        if (!pInstance) {
            return;
        }
        pInstance->Render(pInstance->presenters[glutGetWindow()]);
    }

    static void CloseWindowProc() {
        if (!pInstance) {
            return;
        }
        // Enable the flag to break glutMainLoopEvent
        pInstance->stop = true;
    }
    static void KeyboardProc(unsigned char key, int x, int y) {
        if (glutGetModifiers() & GLUT_ACTIVE_ALT && key == '\r') {
            glutFullScreenToggle();
        }
    }

    /**
    *   @brief  This function is responsible for OpenGL/glew/glut initialization and also for initiating display loop
    *   @return void
    */
    inline static bool is_init = false;
    void Run(int num_presenters) {

        int argc = 1;
        const char *argv[] = {"dummy"};
        glutInit(&argc, (char **)argv);
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

            glutKeyboardFunc(KeyboardProc);

            presenters.emplace(new_info.window_id, new_info);
        }
        
        LOG_INFO("All presenters registered, beginning render");
        pInstance = this;
        // Launch the rendering loop
        while (!stop) {
            glutMainLoopEvent();
        }
        pInstance = NULL;

        for (auto &[id, info] : presenters) {
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
    void Render(PresenterInfo& info) {
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
        } else {
            inner_window_width = window_width;
            inner_window_height = window_width / source_ratio;
        }
        int inner_window_x = (window_width / 2) - (inner_window_width / 2);
        int inner_window_y = (window_height / 2) - (inner_window_height / 2);

        float left, right, top, bottom;

        left = inner_window_x / static_cast<float>(window_width);
        right = (inner_window_x + inner_window_width) / static_cast<float>(window_width);
        top = inner_window_y / static_cast<float>(window_height);
        bottom = (inner_window_y + inner_window_height) / static_cast<float>(window_height);

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

    static void PrintText(int iFont, std::string strText, int x, int y, bool bFillBackground) {
        struct {void *font; int d1; int d2;} fontData[] = {
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
        void *font = fontData[iFont].font;
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

private:

    bool stop = false;
    std::map<int, PresenterInfo> presenters;
    inline static moodycamel::BlockingConcurrentQueue<PresenterInfo> new_presenter_queue;
    std::unique_ptr<std::thread> message_loop = NULL;
};
