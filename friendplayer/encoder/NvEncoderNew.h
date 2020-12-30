#pragma once

#include "nvEncodeAPI.h"

#include <array>
#include <d3d11.h>
#include <mutex>
#include <string>
#include <stdint.h>
#include <vector>

struct NvEncInputFrame {
    ID3D11Texture2D* input_ptr = nullptr;
    uint32_t chroma_offsets[2];
    uint32_t num_chroma_planes;
    uint32_t pitch;
    uint32_t chroma_pitch;
    NV_ENC_BUFFER_FORMAT buffer_format;
    NV_ENC_INPUT_RESOURCE_TYPE resource_type;
};

class NvEncoderNew
{
public:
    NvEncoderNew(ID3D11Device* device, uint32_t width, uint32_t height, NV_ENC_BUFFER_FORMAT buffer_fmt);

    bool CreateEncoder(const NV_ENC_INITIALIZE_PARAMS* init_params);
    void DestroyEncoder();
    bool Reconfigure(const NV_ENC_RECONFIGURE_PARAMS *reconfig_arams);

    const NvEncInputFrame* GetActiveFrame();
    const NvEncInputFrame* GetStagingFrame();
    void Swap();

    void EncodeActiveFrame(std::vector<std::vector<uint8_t>> &packets_out, NV_ENC_PIC_PARAMS *pic_params = nullptr);

    void EndEncode(std::vector<std::vector<uint8_t>> &packets_out);

    int GetCapabilityValue(GUID guid_codec, NV_ENC_CAPS caps_to_query);
    void *GetDevice() const { return device; }


    NV_ENC_DEVICE_TYPE GetDeviceType() const { return dev_type; }

    int GetEncodeWidth() const { return width; }
    int GetEncodeHeight() const { return height; }

    int GetFrameSize() const;

    bool CreateDefaultEncoderParams(NV_ENC_INITIALIZE_PARAMS* init_params, GUID codec_guid, GUID preset_guid, NV_ENC_TUNING_INFO tuning_info = NV_ENC_TUNING_INFO_UNDEFINED);
    void GetInitializeParams(NV_ENC_INITIALIZE_PARAMS *init_params);

    void GetSequenceParams(std::vector<uint8_t> &sps);

    ~NvEncoderNew();

public:
    static void GetChromaSubPlaneOffsets(const NV_ENC_BUFFER_FORMAT buffer_fmt, const uint32_t pitch,
                                        const uint32_t height, std::vector<uint32_t>& chroma_offsets);
    static uint32_t GetChromaPitch(const NV_ENC_BUFFER_FORMAT bufferFormat, const uint32_t lumaPitch);
    static uint32_t GetNumChromaPlanes(const NV_ENC_BUFFER_FORMAT bufferFormat);
    static uint32_t GetChromaWidthInBytes(const NV_ENC_BUFFER_FORMAT bufferFormat, const uint32_t lumaWidth);
    static uint32_t GetChromaHeight(const NV_ENC_BUFFER_FORMAT bufferFormat, const uint32_t lumaHeight);
    static uint32_t GetWidthInBytes(const NV_ENC_BUFFER_FORMAT bufferFormat, const uint32_t width);

protected:
    void RegisterInputResources(std::vector<void*> inputframes, NV_ENC_INPUT_RESOURCE_TYPE eResourceType,
        int width, int height, int pitch, NV_ENC_BUFFER_FORMAT bufferFormat, bool bReferenceFrame = false);
    void UnregisterInputResources();
    NV_ENC_REGISTERED_PTR RegisterResource(void *pBuffer, int width, int height,
        int pitch, NV_ENC_BUFFER_FORMAT buffer_fmt, NV_ENC_BUFFER_USAGE buffer_usage = NV_ENC_INPUT_IMAGE);

    uint32_t GetMaxEncodeWidth() const { return max_enc_width; }
    uint32_t GetMaxEncodeHeight() const { return max_enc_height; }
    NV_ENC_BUFFER_FORMAT GetPixelFormat() const { return buffer_fmt; }

    void* GetCompletionEvent() { 
        return (m_vpCompletionEvent.size() == m_nEncoderBuffer) ? m_vpCompletionEvent[eventIdx] : nullptr; }

    NVENCSTATUS DoEncode(NV_ENC_INPUT_PTR input_buffer, NV_ENC_OUTPUT_PTR output_buffer, NV_ENC_PIC_PARAMS *in_params = nullptr);

    void MapResources();
    void WaitForCompletionEvent(int iEvent);
    void SendEOS();

private:
    bool LoadNvEncApi();

    void GetEncodedPacket(std::vector<NV_ENC_OUTPUT_PTR> &vOutputBuffer, std::vector<std::vector<uint8_t>> &vPacket, bool bOutputDelay);

    void InitializeBitstreamBuffer();

    void DestroyBitstreamBuffer();

    void InitializeMVOutputBuffer();

    void DestroyMVOutputBuffer();

    void DestroyHWEncoder();

    void FlushEncoder();

protected:
    void* encoder_ptr = nullptr;
    NV_ENCODE_API_FUNCTION_LIST nvenc_fns;
    std::array<NvEncInputFrame, 2> input_frames;
    std::array<NV_ENC_REGISTERED_PTR, 2> registered_resources;
    std::array<NV_ENC_REGISTERED_PTR, 2> registered_resources_for_reference;
    std::array<NV_ENC_INPUT_PTR, 2> m_vMappedInputBuffers;
    std::array<void*, 2> m_vpCompletionEvent;

    int active_input_frame = 0;
    int32_t got_packets = 0;

private:
    uint32_t width;
    uint32_t height;
    NV_ENC_BUFFER_FORMAT buffer_fmt;
    ID3D11Device* device;
    NV_ENC_DEVICE_TYPE dev_type;
    NV_ENC_INITIALIZE_PARAMS init_params = {};
    NV_ENC_CONFIG encode_cfg = {};
    bool encoder_initialized = false;
    std::vector<NV_ENC_OUTPUT_PTR> m_vBitstreamOutputBuffer;
    std::vector<NV_ENC_OUTPUT_PTR> m_vMVDataOutputBuffer;
    uint32_t max_enc_width = 0;
    uint32_t max_enc_height = 0;
};
