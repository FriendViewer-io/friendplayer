#pragma once

#include <cuda.h>
#include <vector>
#include <memory>
#include <string>

#include "nvEncodeAPI.h"

class NvEncoder;
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;
class DDAImpl;

class NvDecoder;
class FramePresenterD3D11;

class UDPSocket;

class VideoStreamer {
public:
    VideoStreamer();
    ~VideoStreamer();

    bool InitEncode(int monitor_idx);
    void Encode(bool send_idr, bool send_pps_sps, std::string& data_out);

    bool InitDecode();
    bool InitDisplay();
    void Decode(std::string* video_packet);
    void PresentVideo();
    bool IsDisplayInit();

private:
    // Decoder
    CUcontext cuda_context;
    CUdeviceptr cuda_frame;
    NvDecoder* decoder = nullptr;
    FramePresenterD3D11* presenter = nullptr;
    int num_frames;
    bool display_init;

    // Encoder
    bool InitEncoderParams(int frames_per_sec, int avg_bitrate, DWORD w, DWORD h);

    template <typename T>
    using dx_unique_ptr = std::unique_ptr<T, void(*)(T*)>;
    dx_unique_ptr<ID3D11Device> d3d_dev;
    dx_unique_ptr<ID3D11DeviceContext> d3d_ctx;
    dx_unique_ptr<ID3D11Texture2D> dxgi_tex;
    dx_unique_ptr<ID3D11Texture2D> nvenc_buf;

    NV_ENC_INITIALIZE_PARAMS init_params;
    NV_ENC_CONFIG enc_cfg;

    std::unique_ptr<NvEncoder> nvenc;
    std::unique_ptr<DDAImpl> dxgi_provider;

    ID3D11Texture2D* cur_frame = nullptr;

    uint32_t encoder_frame_num = 0;

    // Common
    LARGE_INTEGER counter_freq;
};