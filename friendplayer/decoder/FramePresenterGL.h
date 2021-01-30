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

#include <bitset>
#include <string.h>
#include <map>
#include <thread>
#include <GL/glew.h>
#include <cuda.h>
#include <concurrentqueue/blockingconcurrentqueue.h>

class HostActor;
struct GLFWwindow;

/**
* @brief OpenGL utility class to display decoded frames using OpenGL textures
*/
class FramePresenterGL
{
private:
    struct PresenterInfo {
        int width = 0, height = 0;
        int window_width, window_height, window_x, window_y;
        GLFWwindow* window = nullptr;
        CUcontext context = NULL;
        CUdeviceptr frame;
        CUgraphicsResource cuResource;
        int stream_num;
        std::string text;
        GLuint pbo;
        GLuint tex;
        GLuint shader;
        struct {
            int left;
            int right;
            int top;
            int bottom;
        } display_rect;
        bool fullscreen_state;
    };
public:

    /**
    *   @brief  FramePresenterGL constructor function. This will launch a rendering thread which will be fed by decoded frames
    *   @param  cuContext - CUDA Context handle
    *   @param  nWidth - Width of OpenGL texture
    *   @param  nHeight - Height of OpenGL texture
    */
    FramePresenterGL(HostActor* callback_inst, int num_presenters)
        : callback_inst(callback_inst)
    {
        message_loop = std::make_unique<std::thread>(&FramePresenterGL::Run, this, num_presenters);

        // This loop will ensure that OpenGL/glew/glut initialization is finished before we return from this constructor
        /*while (!pInstance) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }*/
    }

    /**
    *   @brief  Destructor function. Also breaks glutMainLoopEvent
    */
    ~FramePresenterGL() {
        Stop();
        Cleanup();
    }

    static CUdeviceptr RegisterContext(CUcontext context, int width, int height, int stream_num);

private:
     /**
     *   @brief  Registered with glutDisplayFunc() as rendering function
     *   @return void
     */

    static void KeyProc(GLFWwindow* window, int key, int scancode, int action, int mods);

    static void MouseButtonProc(GLFWwindow* window, int button, int action, int mods);

    static void MousePosProc(GLFWwindow* window, double x, double y);

    static void MouseWheelProc(GLFWwindow* window, double x_offset, double y_offset);

    static void WindowCloseProc(GLFWwindow* window);

    /**
    *   @brief  This function is responsible for OpenGL/glew/glut initialization and also for initiating display loop
    *   @return void
    */
    void Run(int num_presenters);
    /**
    *   @brief  Rendering function called by glut
    *   @return void
    */
    void Render(PresenterInfo& info);

    bool TranslateCoords(PresenterInfo& info, double& x, double& y);

    void Cleanup();

    void Stop();

    static void PrintText(int iFont, std::string strText, int x, int y, bool bFillBackground);

private:

    bool stop = false;
    std::map<GLFWwindow*, PresenterInfo> presenters;
    inline static moodycamel::BlockingConcurrentQueue<PresenterInfo> new_presenter_queue;
    std::unique_ptr<std::thread> message_loop = NULL;
    std::bitset<12> mouse_press_map;
    std::bitset<256> key_press_map;
    HostActor* callback_inst;
    GLFWwindow* main_window = NULL;
};
