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
#include <cuda.h>
#include "common/NvCodecUtils.h"

/**
* @brief Base class for D3D presentation of decoded frames
*/
class FramePresenterD3D {
public:
    /**
    *   @brief  FramePresenterD3D constructor.
    *   @param  cuContext - CUDA context handle
    *   @param  nWidth - Width of D3D Surface
    *   @param  nHeight - Height of D3D Surface
    */
    FramePresenterD3D(CUcontext cuContext, int nWidth, int nHeight, int windowWidth = 0, int windowHeight = 0) : cuContext(cuContext), nWidth(nWidth), nHeight(nHeight), nWindowWidth(windowWidth), nWindowHeight(windowHeight) {
        self = this;
        // Start out at 75% of current resolution size
        if (!windowWidth && !windowHeight) {
            float aspect_ratio = static_cast<float>(nWidth) / nHeight;
            nWindowWidth = static_cast<int>(GetSystemMetrics(SM_CXSCREEN) * 0.75f);
            nWindowHeight = static_cast<int>(nWindowWidth / aspect_ratio);
        }
    }
    /**
    *   @brief  FramePresenterD3D destructor.
    */
    virtual ~FramePresenterD3D() {};
    /**
    *   @brief  Pure virtual to be implemented by derived classes. Should present decoded frames available in device memory
    *   @param  dpBgra - CUDA device pointer to BGRA surface
    *   @param  nPitch - pitch of the BGRA surface. Typically width in pixels * number of bytes per pixel
    *   @param  delay  - presentation delay. Cue to D3D presenter to inform about the time at which this frame needs to be presented
    *   @return true on success
    *   @return false on failure
    */
    virtual bool PresentDeviceFrame(unsigned char* dpBgra, int nPitch) = 0;

protected:
    /**
    *   @brief  Create and show D3D window.
    *   @param  nWidth   - Width of the window
    *   @param  nHeight  - Height of the window
    *   @return hwndMain - handle to the created window
    */
    static HWND CreateAndShowWindow(int width, int height) {

        static char szAppName[] = "D3DPresenter";
        static char szAppNameChild[] = "D3DPresenterChild";
        WNDCLASS wndclass;
        wndclass.style = CS_HREDRAW | CS_VREDRAW;
        wndclass.lpfnWndProc = WndProc;
        wndclass.cbClsExtra = 0;
        wndclass.cbWndExtra = 0;
        wndclass.hInstance = (HINSTANCE)GetModuleHandle(NULL);
        wndclass.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        wndclass.hCursor = LoadCursor(NULL, IDC_ARROW);
        wndclass.hbrBackground = CreateSolidBrush(RGB(0, 0, 0));
        wndclass.lpszMenuName = NULL;
        wndclass.lpszClassName = szAppName;
        RegisterClass(&wndclass);

        WNDCLASS wndclass_child;
        wndclass_child.style = CS_HREDRAW | CS_VREDRAW;
        wndclass_child.lpfnWndProc = WndProc2;
        wndclass_child.cbClsExtra = 0;
        wndclass_child.cbWndExtra = 0;
        wndclass_child.hInstance = (HINSTANCE)GetModuleHandle(NULL);
        wndclass_child.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        wndclass_child.hCursor = LoadCursor(NULL, IDC_ARROW);
        wndclass_child.hbrBackground = NULL;
        wndclass_child.lpszMenuName = NULL;
        wndclass_child.lpszClassName = szAppNameChild;
        RegisterClass(&wndclass_child);

        RECT rc{
            (GetSystemMetrics(SM_CXSCREEN) - width) / 2,
            (GetSystemMetrics(SM_CYSCREEN) - height) / 2,
            (GetSystemMetrics(SM_CXSCREEN) + width) / 2,
            (GetSystemMetrics(SM_CYSCREEN) + height) / 2
        };
        AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

        HWND hwndMain = CreateWindow(szAppName, szAppName, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
            rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
            NULL, NULL, wndclass.hInstance, NULL);

        int x = (rc.right - rc.left) - width;
        int y = (rc.bottom - rc.top) - height;
        HWND childWindow = CreateWindow(szAppNameChild, szAppNameChild, WS_CHILD | WS_VISIBLE,
            0, 0, width, height,
            hwndMain, NULL, wndclass.hInstance, NULL);
        ShowWindow(hwndMain, SW_SHOW);
        UpdateWindow(hwndMain);
        abc = childWindow;

        return childWindow;
    }

    /**
    *   @brief  Copy device frame to cuda registered D3D surface. More specifically, this function maps the
    *           D3D swap chain backbuffer into a cuda array and copies the contents of dpBgra into it.
    *           This ensures that the swap chain back buffer will contain the next surface to be presented.
    *   @param  dpBgra  - CUDA device pointer to BGRA surface
    *   @param  nPitch  - pitch of the BGRA surface. Typically width in pixels * number of bytes per pixel
    */
    void CopyDeviceFrame(unsigned char* dpBgra, int nPitch) {
        check(cuCtxPushCurrent(cuContext));
        check(cuGraphicsMapResources(1, &cuResource, 0));
        CUarray dstArray;
        check(cuGraphicsSubResourceGetMappedArray(&dstArray, cuResource, 0, 0));

        CUDA_MEMCPY2D m = { 0 };
        m.srcMemoryType = CU_MEMORYTYPE_DEVICE;
        m.srcDevice = (CUdeviceptr)dpBgra;
        m.srcPitch = nPitch ? nPitch : nWidth * 4;
        m.dstMemoryType = CU_MEMORYTYPE_ARRAY;
        m.dstArray = dstArray;
        m.WidthInBytes = nWidth * 4;
        m.Height = nHeight;
        check(cuMemcpy2D(&m));

        check(cuGraphicsUnmapResources(1, &cuResource, 0));
        check(cuCtxPopCurrent(NULL));
    }

    virtual void HandleResize(WPARAM wParam, RECT& lParam)= 0;

private:
    /**
    *   @brief  Callback called by D3D runtime. This callback is registered during window creation.
    *           On Window close this function posts a quit message. The thread runs to completion once
    *           it retrieves this quit message in its message queue. Refer to Run() function in each of the
    *           derived classes.
    *   @param  hwnd   - handle to the window
    *   @param  msg    - The message sent to this window
    *   @param  wParam - Message specific additional information (No Op in this case)
    *   @param  lParam - Message specific additional information (No Op on this case)
    *   @return 0 on posting quit message
    *   @return result of default window procedure in cases other than the above
    */
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
        case WM_CLOSE:
            PostQuitMessage(0);
            return 0;
        case WM_SIZING:
            //self->HandleResize(wParam, *(RECT*) lParam);
            break;
        case WM_SIZE:
            if (hwnd != abc && (wParam == SIZE_MAXIMIZED || wParam == SIZE_RESTORED)) {
                RECT rect{ 0, 0, lParam & 0xFFFF, lParam >> 16 };
                self->HandleResize(wParam, rect);
            }
            break;
        }
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    static LRESULT CALLBACK WndProc2(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
        case WM_CLOSE:
            PostQuitMessage(0);
            return 0;
        case WM_SIZING:
            //self->HandleResize(wParam, *(RECT*) lParam);
            break;
        }
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

protected:
    int nWidth = 0, nHeight = 0;
    int nWindowWidth, nWindowHeight;
    CUcontext cuContext = NULL;
    CUgraphicsResource cuResource = NULL;
    inline static FramePresenterD3D* self = nullptr;
    HWND childWindow;
    inline static HWND abc = 0;
};
