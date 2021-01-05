#include "encoder/Defs.h"
#include "encoder/DDAImpl.h"
#include "encoder/NvEncoder.h"
#include <iomanip>
#include "common/Log.h"
#include "common/Timer.h"

#define SAFE_RELEASE(X) if(X){X->Release(); X=nullptr;}

/// Initialize DDA
HRESULT DDAImpl::Init()
{
    IDXGIOutput * pOutput = nullptr;
    IDXGIDevice2* pDevice = nullptr;
    IDXGIFactory1* pFactory = nullptr;
    IDXGIAdapter *pAdapter = nullptr;
    IDXGIOutput1* pOut1 = nullptr;

    /// Release all temporary refs before exit
#define CLEAN_RETURN(x) \
    SAFE_RELEASE(pDevice);\
    SAFE_RELEASE(pFactory);\
    SAFE_RELEASE(pOutput);\
    SAFE_RELEASE(pOut1);\
    SAFE_RELEASE(pAdapter);\
    return x;

    HRESULT hr = S_OK;
    /// To create a DDA object given a D3D11 device, we must first get to the DXGI Adapter associated with that device
    if (FAILED(hr = pD3DDev->QueryInterface(__uuidof(IDXGIDevice2), (void**)&pDevice)))
    {
        LOG_CRITICAL("bad1");
        CLEAN_RETURN(hr);
    }

    if (FAILED(hr = pDevice->GetParent(__uuidof(IDXGIAdapter), (void**)&pAdapter)))
    {
        LOG_CRITICAL("bad2");
        CLEAN_RETURN(hr);
    }
    /// Once we have the DXGI Adapter, we enumerate the attached display outputs, and select which one we want to capture
    /// This sample application always captures the primary display output, enumerated at index 0.
    if (FAILED(hr = pAdapter->EnumOutputs(monitor_idx, &pOutput)))
    {
        LOG_CRITICAL("bad3");
        CLEAN_RETURN(hr);
    }

    if (FAILED(hr = pOutput->QueryInterface(__uuidof(IDXGIOutput1), (void**)&pOut1)))
    {
        LOG_CRITICAL("bad4");
        CLEAN_RETURN(hr);
    }
    /// Ask DXGI to create an instance of IDXGIOutputDuplication for the selected output. We can now capture this display output
    if (FAILED(hr = pOut1->DuplicateOutput(pDevice, &pDup)))
    {
        LOG_CRITICAL("bad5");
        CLEAN_RETURN(hr);
    }

    DXGI_OUTDUPL_DESC outDesc;
    ZeroMemory(&outDesc, sizeof(outDesc));
    pDup->GetDesc(&outDesc);

    height = outDesc.ModeDesc.Height;
    width = outDesc.ModeDesc.Width;
    ready_for_capture = true;
    CLEAN_RETURN(hr);
}

/// Acquire a new frame from DDA, and return it as a Texture2D object.
/// 'wait' specifies the time in milliseconds that DDA shoulo wait for a new screen update.
HRESULT DDAImpl::GetCapturedFrame(ID3D11Texture2D **ppTex2D, int wait)
{
    HRESULT hr = S_OK;
    DXGI_OUTDUPL_FRAME_INFO frameInfo;
    ZeroMemory(&frameInfo, sizeof(frameInfo));
    int acquired = 0;
    

#define RETURN_ERR(x) {/*printf(__FUNCTION__": %d : Line %d return 0x%x\n", frameno, __LINE__, x)*/;return x;}

    if (pResource)
    {
        pDup->ReleaseFrame();
        pResource->Release();
        pResource = nullptr;
    }

    auto start_tm = std::chrono::system_clock::now();
    auto wait_tm = std::chrono::milliseconds(wait);

    hr = pDup->AcquireNextFrame(0, &frameInfo, &pResource);

    if (FAILED(hr))
    {
        if (hr == DXGI_ERROR_WAIT_TIMEOUT)
        {
            //printf(__FUNCTION__": %d : Wait for %d ms timed out\n", frameno, wait);
        }
        if (hr == DXGI_ERROR_INVALID_CALL)
        {
            //printf(__FUNCTION__": %d : Invalid Call, previous frame not released?\n", frameno);
        }
        if (hr == DXGI_ERROR_ACCESS_LOST)
        {
            //printf(__FUNCTION__": %d : Access lost, frame needs to be released?\n", frameno);
        }
        RETURN_ERR(hr);
    }
    if (frameInfo.AccumulatedFrames == 0 || frameInfo.LastPresentTime.QuadPart == 0)
    {
        // No image update, only cursor moved.
        RETURN_ERR(DXGI_ERROR_WAIT_TIMEOUT);
    }

    if (!pResource)
    {
        printf(__FUNCTION__": %d : Null output resource. Return error.\n", frameno);
        return E_UNEXPECTED;
    }

    if (FAILED(hr = pResource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)ppTex2D)))
    {
        return hr;
    }

    LARGE_INTEGER pts = frameInfo.LastPresentTime;  MICROSEC_TIME(pts, qpcFreq);
    LONGLONG interval = pts.QuadPart - lastPTS.QuadPart;

    //printf(__FUNCTION__": %d : Accumulated Frames %u PTS Interval %lld PTS %lld\n", frameno, frameInfo.AccumulatedFrames,  interval * 1000, frameInfo.LastPresentTime.QuadPart);
    lastPTS = pts; // store microsec value
    frameno += frameInfo.AccumulatedFrames;
    return hr;
}

/// Release all resources
int DDAImpl::Cleanup()
{
    if (pResource)
    {
        pDup->ReleaseFrame();
        SAFE_RELEASE(pResource);
    }

    width = height = frameno = 0;

    SAFE_RELEASE(pDup);
    SAFE_RELEASE(pCtx);
    SAFE_RELEASE(pD3DDev);
    ready_for_capture = false;

    return 0;
}

void DDAImpl::CaptureFrameLoop(NvEncoder* encoder) {
   using namespace std::chrono_literals;
   Timer capture_timer;
   capture_timer.Start(2000);
   for(;; capture_timer.Synchronize()) {
       HRESULT hr;
        if (!ready_for_capture) {
            hr = Init();
            if (FAILED(hr)) {
                continue;
            }
        }
        if (cur_capture != nullptr) {
            cur_capture->Release();
        }
        cur_capture = nullptr;
        hr = GetCapturedFrame(&cur_capture, 0);
        if (FAILED(hr)) {
            // check for the system crasher
            if (hr != DXGI_ERROR_WAIT_TIMEOUT) {
                if (pResource) {
                    pDup->ReleaseFrame();
                    SAFE_RELEASE(pResource);
                }
                ready_for_capture = false;
                width = height = 0;
                SAFE_RELEASE(pDup);
                hr = Init();
                if (SUCCEEDED(hr)) {
                    hr = GetCapturedFrame(&cur_capture, 0);
                }
            }
        }
        if (SUCCEEDED(hr)) {
            encoder->Lock();
            NvEncInputFrame* input = encoder->GetStagingFrame();

            input->input_ptr->AddRef();
            D3D11_QUERY_DESC q;
            q.Query = D3D11_QUERY_EVENT;
            q.MiscFlags = 0;
            ID3D11Query* query_inst;
            auto hr = pD3DDev->CreateQuery(&q, &query_inst);
            pCtx->CopySubresourceRegion(input->input_ptr, D3D11CalcSubresource(0, 0, 1), 0, 0, 0, cur_capture, 0, nullptr);
            pCtx->End(query_inst);

            while (pCtx->GetData(query_inst, nullptr, 0, 0) == S_FALSE) {
            }
            input->input_ptr->Release();
            query_inst->Release();
            encoder->PostSwap();
            encoder->Unlock();
        }
   }
}