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
#include <mutex>
#include <thread>
#include <d3d11.h>
#include <cuda.h>
#include <cudaD3D11.h>
#include <numeric>
#include "FramePresenterD3D.h"
#include "common/NvCodecUtils.h"

/**
* @brief D3D11 presenter class derived from FramePresenterD3D
*/
class FramePresenterD3D11 : public FramePresenterD3D
{
public:
    /**
    *   @brief  FramePresenterD3D11 constructor. This will launch a rendering thread which will be fed with decoded frames
    *   @param  cuContext - CUDA context handle
    *   @param  nWidth - Width of D3D surface
    *   @param  nHeight - Height of D3D surface
    */
    FramePresenterD3D11(CUcontext cuContext, int nWidth, int nHeight, int windowWidth = 0, int windowHeight = 0) :
        FramePresenterD3D(cuContext, nWidth, nHeight, windowWidth, windowHeight)
    {
        pthMsgLoop = new std::thread(ThreadProc, this);
        while (!bReady) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        hTimerQueue = CreateTimerQueue();
        hPresentEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    }

    /**
    *   @brief  FramePresenterD3D11 destructor.
    */
    ~FramePresenterD3D11() {
        if (hTimerQueue)
        {
            DeleteTimerQueue(hTimerQueue);
        }
        if (hPresentEvent)
        {
            CloseHandle(hPresentEvent);
        }
        bQuit = true;
        pthMsgLoop->join();
        delete pthMsgLoop;
    }

    bool PresentDeviceFrame(unsigned char* dpBgra, int nPitch, int64_t delay) {
        mtx.lock();
        if (!bReady) {
            mtx.unlock();
            return false;
        }
        CopyDeviceFrame(dpBgra, nPitch);
        /*if (!CreateTimerQueueTimer(&hTimer, hTimerQueue,
            (WAITORTIMERCALLBACK)PresentRoutine, this, (DWORD)delay, 0, 0))
        {
            std::cout << "Problem in createtimer" << std::endl;
        }
        while (WaitForSingleObject(hPresentEvent, 0) != WAIT_OBJECT_0)
        {
        }
        if (hTimer)
        {
            DeleteTimerQueueTimer(hTimerQueue, hTimer, nullptr);
        }*/
        pSwapChain->Present(0, 0);
        mtx.unlock();
        return true;
    }

    void HandleResize(WPARAM edge, RECT& rect)
    {
        int size_x_desired = static_cast<int>((rect.right - rect.left) - window_adjust_x);
        int size_y_desired = static_cast<int>((rect.bottom - rect.top) - window_adjust_y);

        switch (edge)
        {
        case WMSZ_BOTTOM:
        case WMSZ_TOP:
        {
            int size_x = static_cast<int>(window_adjust_x + (size_y_desired * window_ratio_x) / window_ratio_y);
            rect.left = (rect.left + rect.right) / 2 - size_x / 2;
            rect.right = rect.left + size_x;
        }
        break;
        case WMSZ_BOTTOMLEFT:
        {
            int size_x, size_y;

            if (size_x_desired * window_ratio_y > size_y_desired * window_ratio_x)
            {
                size_x = rect.right - rect.left;
                size_y = static_cast<int>(window_adjust_y + ((size_x - window_adjust_x) * window_ratio_y) / window_ratio_x);
            }
            else
            {
                size_y = rect.bottom - rect.top;
                size_x = static_cast<int>(window_adjust_x + ((size_y - window_adjust_y) * window_ratio_x) / window_ratio_y);
            }

            rect.left = rect.right - size_x;
            rect.bottom = rect.top + size_y;
        }
        break;
        case WMSZ_BOTTOMRIGHT:
        {
            int size_x, size_y;

            if (size_x_desired * window_ratio_y > size_y_desired * window_ratio_x)
            {
                size_x = rect.right - rect.left;
                size_y = static_cast<int>(window_adjust_y + ((size_x - window_adjust_x) * window_ratio_y) / window_ratio_x);
            }
            else
            {
                size_y = rect.bottom - rect.top;
                size_x = static_cast<int>(window_adjust_x + ((size_y - window_adjust_y) * window_ratio_x) / window_ratio_y);
            }

            rect.right = rect.left + size_x;
            rect.bottom = rect.top + size_y;
        }
        break;
        case WMSZ_LEFT:
        case WMSZ_RIGHT:
        {
            int size_y = static_cast<int>(window_adjust_y + (size_x_desired * window_ratio_y) / window_ratio_x);
            rect.top = (rect.top + rect.bottom) / 2 - size_y / 2;
            rect.bottom = rect.top + size_y;
        }
        break;
        case WMSZ_TOPLEFT:
        {
            int size_x, size_y;

            if (size_x_desired * window_ratio_y > size_y_desired * window_ratio_x)
            {
                size_x = rect.right - rect.left;
                size_y = static_cast<int>(window_adjust_y + ((size_x - window_adjust_x) * window_ratio_y) / window_ratio_x);
            }
            else
            {
                size_y = rect.bottom - rect.top;
                size_x = static_cast<int>(window_adjust_x + ((size_y - window_adjust_y) * window_ratio_x) / window_ratio_y);
            }

            rect.left = rect.right - size_x;
            rect.top = rect.bottom - size_y;
        }
        break;
        case WMSZ_TOPRIGHT:
        {
            int size_x, size_y;

            if (size_x_desired * window_ratio_y > size_y_desired * window_ratio_x)
            {
                size_x = rect.right - rect.left;
                size_y = static_cast<int>(window_adjust_y + ((size_x - window_adjust_x) * window_ratio_y) / window_ratio_x);
            }
            else
            {
                size_y = rect.bottom - rect.top;
                size_x = static_cast<int>(window_adjust_x + ((size_y - window_adjust_y) * window_ratio_x) / window_ratio_y);
            }

            rect.right = rect.left + size_x;
            rect.top = rect.bottom - size_y;
        }
        break;
        }
    }

private:
    /**
    *   @brief  Launches the windowing functionality
    *   @param  This - pointer to FramePresenterD3D11 object
    */
    static void ThreadProc(FramePresenterD3D11* This) {
        This->Run();
    }
    /**
    *   @brief  Callback called by D3D runtime. This callback is registered during call to
    *           CreateTimerQueueTimer in PresentDeviceFrame. In CreateTimerQueueTimer we also
    *           set a timer. When this timer expires this callback is called. This functionality
    *           is present to facilitate timestamp based presentation.
    *   @param  lpParam - void pointer to client data
    *   @param  TimerOrWaitFired - TRUE for this callback as this is a Timer based callback (Refer:https://docs.microsoft.com/en-us/previous-versions/windows/desktop/legacy/ms687066(v=vs.85))
    */
    static VOID CALLBACK PresentRoutine(PVOID lpParam, BOOLEAN TimerOrWaitFired)
    {
        if (!lpParam) return;
        FramePresenterD3D11* presenter = (FramePresenterD3D11*)lpParam;
        presenter->pSwapChain->Present(1, 0);
        SetEvent(presenter->hPresentEvent);
    }

    /**
    *   @brief This function is on a thread spawned during FramePresenterD3D11 construction.
    *          It creates the D3D window and monitors window messages in a loop. This function
    *          also creates swap chain for presentation and also registers the swap chain backbuffer
    *          with cuda.
    */
    void Run() {
        HWND hwndMain = CreateAndShowWindow(nWindowWidth, nWindowHeight);

        DXGI_SWAP_CHAIN_DESC sc = { 0 };
        sc.BufferCount = 1;
        sc.BufferDesc.Width = nWidth;
        sc.BufferDesc.Height = nHeight;
        sc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        sc.BufferDesc.RefreshRate.Numerator = 0;
        sc.BufferDesc.RefreshRate.Denominator = 1;
        sc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sc.OutputWindow = hwndMain;
        sc.SampleDesc.Count = 1;
        sc.SampleDesc.Quality = 0;
        sc.Windowed = TRUE;

        // Determine resizing aspect ratio numbers
        window_ratio_x = nWidth / std::gcd(nWidth, nHeight);
        window_ratio_y = nHeight / std::gcd(nWidth, nHeight);

        RECT rect = { 0, 0, nWidth, nHeight };
        AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
        window_adjust_x = (rect.right - rect.left) - nWidth;
        window_adjust_y = (rect.bottom - rect.top) - nHeight;


        ID3D11Device* pDevice = NULL;
        check(D3D11CreateDeviceAndSwapChain(GetAdapterByContext(cuContext), D3D_DRIVER_TYPE_UNKNOWN,
            NULL, 0, NULL, 0, D3D11_SDK_VERSION, &sc, &pSwapChain, &pDevice, NULL, &pContext));
        check(pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer));

        // Used for host presenting
        D3D11_TEXTURE2D_DESC td;
        pBackBuffer->GetDesc(&td);
        td.BindFlags = 0;
        td.Usage = D3D11_USAGE_STAGING;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        check(pDevice->CreateTexture2D(&td, NULL, &pStagingTexture));


        // Map cuda frame buffer to swapchain backbuffer
        check(cuCtxPushCurrent(cuContext));
        check(cuGraphicsD3D11RegisterResource(&cuResource, pBackBuffer, CU_GRAPHICS_REGISTER_FLAGS_NONE));
        check(cuGraphicsResourceSetMapFlags(cuResource, CU_GRAPHICS_MAP_RESOURCE_FLAGS_WRITE_DISCARD));
        check(cuCtxPopCurrent(NULL));

        bReady = true;
        MSG msg = { 0 };
        while (!bQuit && msg.message != WM_QUIT) {
            if (GetMessage(&msg, 0, 0, 0)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }

        mtx.lock();
        bReady = false;
        check(cuCtxPushCurrent(cuContext));
        check(cuGraphicsUnregisterResource(cuResource));
        check(cuCtxPopCurrent(NULL));
        pStagingTexture->Release();
        pBackBuffer->Release();
        pContext->Release();
        pDevice->Release();
        pSwapChain->Release();
        DestroyWindow(hwndMain);
        mtx.unlock();
    }

    /**
    *   @brief  Gets the DXGI adapter on which the given cuda context is current
    *   @param   CUcontext - handle to cuda context
    *   @return  pAdapter - pointer to DXGI adapter
    *   @return  NULL - In case there is no adapter corresponding to the supplied cuda context
    */
    static IDXGIAdapter* GetAdapterByContext(CUcontext cuContext) {
        CUdevice cuDeviceTarget;
        check(cuCtxPushCurrent(cuContext));
        check(cuCtxGetDevice(&cuDeviceTarget));
        check(cuCtxPopCurrent(NULL));

        IDXGIFactory1* pFactory = NULL;
        check(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&pFactory));
        IDXGIAdapter* pAdapter = NULL;
        for (unsigned i = 0; pFactory->EnumAdapters(i, &pAdapter) != DXGI_ERROR_NOT_FOUND; i++) {
            CUdevice cuDevice;
            check(cuD3D11GetDevice(&cuDevice, pAdapter));
            if (cuDevice == cuDeviceTarget) {
                pFactory->Release();
                return pAdapter;
            }
            pAdapter->Release();
        }
        pFactory->Release();
        return NULL;
    }

private:
    bool bReady = false;
    bool bQuit = false;
    std::mutex mtx;
    std::thread* pthMsgLoop = NULL;

    double window_ratio_x;
    double window_ratio_y;
    double window_adjust_x;
    double window_adjust_y;

    IDXGISwapChain* pSwapChain = NULL;
    ID3D11DeviceContext* pContext = NULL;
    ID3D11Texture2D* pBackBuffer = NULL, * pStagingTexture = NULL;
    HANDLE hTimer;
    HANDLE hTimerQueue;
    HANDLE hPresentEvent;
    ID3D11RenderTargetView *pRenderTargetView;
    ID3D11DepthStencilView *pStencilView;
};
