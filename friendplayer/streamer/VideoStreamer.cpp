#include "VideoStreamer.h"

#include <cuda.h>
#include <iostream>
#include <d3d11_4.h>

// Common
#include "common/Log.h"
#include "common/NvCodecUtils.h"
#include "common/ColorSpace.h"
#include "common/Config.h"
#include "common/FrameRingBuffer.h"
#include "protobuf/client_messages.pb.h"

// Decoder
#include "decoder/NvDecoder.h"
#include "decoder/FramePresenterGL.h"

// Encoder
#include "nvEncodeAPI.h"
#include "encoder/DDAImpl.h"
#include "encoder/NvEncoder.h"

namespace {
template<typename T>
void DxDeleter(T* dx_obj) {
    if (dx_obj != nullptr) {
        dx_obj->Release();
    }
}
}

VideoStreamer::VideoStreamer()
    : d3d_dev(nullptr, DxDeleter<ID3D11Device>),
    d3d_ctx(nullptr, DxDeleter<ID3D11DeviceContext>),
    dxgi_tex(nullptr, DxDeleter<ID3D11Texture2D>),
    nvenc_buf(nullptr, DxDeleter<ID3D11Texture2D>) {
    QueryPerformanceFrequency(&counter_freq);
}

VideoStreamer::~VideoStreamer() {
    cuMemFree(cuda_frame);
    if (decoder != nullptr) {
        delete decoder;
    }
}

bool VideoStreamer::InitEncoderParams(int frames_per_sec, int avg_bitrate, DWORD w, DWORD h) {
    LOG_INFO("Initializing NVENC for {}x{} video @ {} frames/second with bitrate {} bits/second", w, h, frames_per_sec, avg_bitrate);
    memset(&init_params, 0, sizeof(NV_ENC_INITIALIZE_PARAMS));
    memset(&enc_cfg, 0, sizeof(NV_ENC_CONFIG));

    init_params.encodeConfig = &enc_cfg;
    init_params.maxEncodeWidth = init_params.encodeWidth = w;
    init_params.maxEncodeHeight = init_params.encodeHeight = h;

    try {
        nvenc->CreateDefaultEncoderParams(&init_params, NV_ENC_CODEC_H264_GUID, NV_ENC_PRESET_P1_GUID, NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY);
    }
    catch (...) {
        LOG_CRITICAL("Failed to create encoder init params!");
        return false;
    }
    init_params.frameRateNum = frames_per_sec;
    init_params.frameRateDen = 1;

    enc_cfg.gopLength = NVENC_INFINITE_GOPLENGTH;
    enc_cfg.frameIntervalP = 1;
    enc_cfg.encodeCodecConfig.h264Config.idrPeriod = NVENC_INFINITE_GOPLENGTH;
    enc_cfg.rcParams.rateControlMode = NV_ENC_PARAMS_RC_CBR;
    enc_cfg.rcParams.multiPass = NV_ENC_TWO_PASS_FULL_RESOLUTION;
    enc_cfg.rcParams.averageBitRate = avg_bitrate;
    enc_cfg.rcParams.vbvBufferSize = (enc_cfg.rcParams.averageBitRate * init_params.frameRateDen / init_params.frameRateNum) * 5;
    enc_cfg.rcParams.maxBitRate = enc_cfg.rcParams.averageBitRate;
    enc_cfg.rcParams.vbvInitialDelay = enc_cfg.rcParams.vbvBufferSize;

    LOG_INFO("Successfully initialized encoder configurations and init params.");
    return true;
}

bool VideoStreamer::InitEncode(int monitor_idx, int& out_monitor_enum_index) {
    LOG_INFO("Called VideoStreamer::InitEncode");
    // InitDXGI
    HRESULT hr = S_OK;
    D3D_DRIVER_TYPE driver_types[] = {
        D3D_DRIVER_TYPE_HARDWARE,
        D3D_DRIVER_TYPE_WARP,
        D3D_DRIVER_TYPE_REFERENCE,
    };
    const UINT num_driver_types = sizeof(driver_types) / sizeof(D3D_DRIVER_TYPE);

    D3D_FEATURE_LEVEL feature_levels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
        D3D_FEATURE_LEVEL_9_1
    };
    const UINT num_feature_levels = sizeof(feature_levels) / sizeof(D3D_FEATURE_LEVEL);
    D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_11_0;

    ID3D11Device* d3d_dev_ptr = nullptr;
    ID3D11DeviceContext* d3d_ctx_ptr = nullptr;
    for (UINT i = 0; i < num_driver_types; ++i) {
        hr = D3D11CreateDevice(nullptr, driver_types[i], nullptr, 0, feature_levels, num_feature_levels,
            D3D11_SDK_VERSION, &d3d_dev_ptr, &feature_level, &d3d_ctx_ptr);
        if (SUCCEEDED(hr)) {
            break;
        }
    }
    if (FAILED(hr)) {
        LOG_CRITICAL("Failed to create D3D11 device, HRESULT=0x{:#x}", hr);
        return false;
    }

    ID3D11Multithread* mt_obj;
    d3d_dev_ptr->QueryInterface(__uuidof(ID3D11Multithread), (void**)&mt_obj);
    mt_obj->SetMultithreadProtected(TRUE);
    mt_obj->Release();

    LOG_INFO("Successfully created D3D11 device");
    d3d_dev = dx_unique_ptr<ID3D11Device>(d3d_dev_ptr, DxDeleter<ID3D11Device>);
    d3d_ctx = dx_unique_ptr<ID3D11DeviceContext>(d3d_ctx_ptr, DxDeleter<ID3D11DeviceContext>);

    // DXGI dup
    dxgi_provider = std::make_unique<DDAImpl>(d3d_dev.get(), d3d_ctx.get(), monitor_idx);
    hr = dxgi_provider->Init();

    if (FAILED(hr)) {
        dxgi_provider.release();
        d3d_dev.release();
        d3d_ctx.release();
        LOG_CRITICAL("Failed to initialize DXGI output duplicator, HRESULT=0x{:#x}", hr);
        return false;
    }

    DWORD width = dxgi_provider->getWidth(), height = dxgi_provider->getHeight();
    LOG_INFO("Successfully created & initialized DXGI output duplicator for monitor {} ({}x{})", monitor_idx, width, height);

    out_monitor_enum_index = dxgi_provider->GetMonitorEnumIndex();

    // InitEnc
    NV_ENC_BUFFER_FORMAT fmt = NV_ENC_BUFFER_FORMAT_ARGB;
    nvenc = std::make_unique<NvEncoder>(d3d_dev.get(), width, height);

    if (!InitEncoderParams(60, Config::AverageBitrate, width, height)) {
        nvenc.release();
        dxgi_provider.release();
        d3d_dev.release();
        d3d_ctx.release();
        return false;
    }

    LOG_INFO("Creating NVENC encoder");
    try {
        nvenc->CreateEncoder(&init_params);
    }
    catch (...) {
        LOG_CRITICAL("Failed to create encoder!");
        return false;
    }
    dxgi_provider->StartCapture(nvenc.get());

    return true;
}

void VideoStreamer::Encode(bool send_idr, bool pps_sps_requested, std::string& data_out) {
    using namespace std::chrono_literals;
    auto begin_time = std::chrono::system_clock::now();
    send_idr = send_idr || encoder_frame_num == 0;
    NV_ENC_PIC_PARAMS params = {};
    if (send_idr) {
        params.encodePicFlags = NV_ENC_PIC_FLAG_FORCEIDR;
    }
    std::vector<std::vector<uint8_t>> packets;

    // auto wait_time = 4ms;
    // while (begin_time + wait_time > std::chrono::system_clock::now() &&
    //     !nvenc->SwapReady());

    nvenc->Lock();
    if (!nvenc->SwapReady()) {
        LOG_TRACE("Re-encoding frame");
    }
    nvenc->Swap();
    nvenc->Unlock();

    // if frame num == 0 then pps/sps inserted already
    if (pps_sps_requested && encoder_frame_num > 0) {
        nvenc->GetSequenceParams(data_out);
    }

    auto pre_enc = std::chrono::system_clock::now();
    nvenc->EncodeActiveFrame(data_out, &params);
    auto post_enc = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - pre_enc);
    if (post_enc.count() > 30000) {
        LOG_CRITICAL("VERY SLOW ENCODE: {} us", post_enc.count());
    }
    else if (post_enc.count() > 20000) {
        LOG_WARNING("SLOW ENCODE: {} us", post_enc.count());
    }

    auto elapsed_time = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - begin_time);
    LOG_TRACE("Successfully encoded frame#{} and retrieved {} packets, {} microseconds elapsed", encoder_frame_num, packets.size(), elapsed_time.count());
    encoder_frame_num++;
}

bool VideoStreamer::InitDecode() {
    LOG_INFO("Initializing decoder");
    if (!check(cuInit(0))) {
        return false;
    }
    CUdevice cuDevice = 0;
    if (check(cuDeviceGet(&cuDevice, 0))) {
        char szDeviceName[80];
        if (check(cuDeviceGetName(szDeviceName, sizeof(szDeviceName), cuDevice))) {
            LOG_INFO("GPU in use: {}", szDeviceName);
        }
        else {
            LOG_WARNING("Could not find GPU device name");
        }
    }
    else {
        LOG_WARNING("Could not find GPU device");
    }
    if (!check(cuCtxCreate(&cuda_context, CU_CTX_SCHED_BLOCKING_SYNC, cuDevice))) {
        LOG_CRITICAL("Failed to create cuda context");
        return false;
    }
    LOG_TRACE("Creating decoder");
    decoder = new NvDecoder(cuda_context, true, cudaVideoCodec_H264, true, false, NULL, NULL, 0, 0, 1000);
    LOG_TRACE("Done initializing decoder");
    display_init = false;
    num_frames = 0;

    return true;
}

bool VideoStreamer::IsDisplayInit() {
    return display_init;
}

bool VideoStreamer::InitDisplay(int stream_num) {
    LOG_TRACE("Registering cuda context");
    cuda_frame = FramePresenterGL::RegisterContext(cuda_context, decoder->GetWidth(), decoder->GetHeight(), stream_num);
    LOG_INFO("Init display stream num {} with {} x {}", stream_num, decoder->GetWidth(), decoder->GetHeight());
    LOG_TRACE("Finished initializing display");
    display_init = true;
    return true;
}

void VideoStreamer::Decode(std::string* video_packet) {
    if (video_packet != nullptr && video_packet->size() > 0 && num_frames == 0) {
        num_frames = decoder->Decode(reinterpret_cast<uint8_t*>(video_packet->data()), video_packet->size(), CUVID_PKT_ENDOFPICTURE);
        LOG_TRACE("Decoded {} frames with {} bytes", num_frames, video_packet->size());
    } else {
        num_frames = 0;
        LOG_INFO("Skipping decode because there were no video bytes");
    }
}

void VideoStreamer::PresentVideo() {
    uint8_t* pFrame;
    int iMatrix = 0;
    int64_t timestamp = 0;
    int nRGBWidth = decoder->GetDecodeWidth();
    
    for (int i = 0; i < num_frames; i++) {
        pFrame = decoder->GetFrame(&timestamp);
        iMatrix = decoder->GetVideoFormatInfo().video_signal_description.matrix_coefficients;

        if (decoder->GetBitDepth() == 8)
        {
            if (decoder->GetOutputFormat() == cudaVideoSurfaceFormat_YUV444)
                YUV444ToColor32<BGRA32>(pFrame, decoder->GetWidth(), (uint8_t*)cuda_frame, 4 * nRGBWidth, decoder->GetWidth(), decoder->GetHeight(), iMatrix);
            else    // default assumed as NV12
                Nv12ToColor32<BGRA32>(pFrame, decoder->GetWidth(), (uint8_t*)cuda_frame, 4 * nRGBWidth, decoder->GetWidth(), decoder->GetHeight(), iMatrix);
        }
        else
        {
            if (decoder->GetOutputFormat() == cudaVideoSurfaceFormat_YUV444_16Bit)
                YUV444P16ToColor32<BGRA32>(pFrame, 2 * decoder->GetWidth(), (uint8_t*)cuda_frame, 4 * nRGBWidth, decoder->GetWidth(), decoder->GetHeight(), iMatrix);
            else // default assumed as P016
                P016ToColor32<BGRA32>(pFrame, 2 * decoder->GetWidth(), (uint8_t*)cuda_frame, 4 * nRGBWidth, decoder->GetWidth(), decoder->GetHeight(), iMatrix);
        }
    }

    num_frames = 0;
}