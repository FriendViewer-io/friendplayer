#pragma once

#include <cuda.h>
#include <vector>
#include <memory>

#include "nvEncodeAPI.h"

class NvEncoderNew;
class ID3D11Device;
class ID3D11DeviceContext;
class ID3D11Texture2D;
class DDAImpl;

class NvDecoder;
class SocketProvider;
class FFmpegDemuxer;
class FramePresenterD3D11;

class UDPSocket;

class Streamer {
public:
    Streamer();
    ~Streamer();

    bool InitEncode();
    void Encode(bool send_idr);
//    void CaptureFrame(int wait_ms);

    bool InitDecode(uint32_t frame_timeout_ms);
    void Demux();
    void Decode();
    void PresentVideo();

    bool InitConnection(const char* ip, unsigned short port, bool is_sender);

private:
    // Decoder
    CUcontext cuda_context;
    CUdeviceptr cuda_frame;
    FFmpegDemuxer* demuxer = nullptr;
    SocketProvider* stream_provider = nullptr;
    NvDecoder* decoder = nullptr;
    FramePresenterD3D11* presenter = nullptr;
    uint8_t* video_packet = nullptr;
    int video_packet_size;
    int64_t video_packet_ts;
    int num_frames;

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

    std::unique_ptr<NvEncoderNew> nvenc;
    std::unique_ptr<DDAImpl> dxgi_provider;

    ID3D11Texture2D* cur_frame = nullptr;

    uint32_t encoder_frame_num = 0;

    // Common    
    UDPSocket* udp_socket;
    LARGE_INTEGER counter_freq;
};