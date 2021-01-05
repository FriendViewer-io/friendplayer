#include "encoder/NvEncoder.h"

#include <D3D9Types.h>
#include "common/Log.h"

NvEncoder::NvEncoder(ID3D11Device* device, uint32_t width, uint32_t height) :
    device(device), 
    dev_type(NV_ENC_DEVICE_TYPE_DIRECTX),
    width(width),
    height(height),
    max_enc_width(width),
    max_enc_height(height),
    buffer_fmt(NV_ENC_BUFFER_FORMAT_ARGB) {

    device->AddRef();
    device->GetImmediateContext(&device_ctx);
    
    if (!LoadNvEncApi()) {
        return;
    }
    if (!nvenc_fns.nvEncOpenEncodeSessionEx) {
        LOG_CRITICAL("EncodeAPI not found");
        return;
    }

    NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS params = { NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER };
    params.device = device;
    params.deviceType = dev_type;
    params.apiVersion = NVENCAPI_VERSION;
    nvenc_fns.nvEncOpenEncodeSessionEx(&params, &encoder_ptr);
}

bool NvEncoder::LoadNvEncApi() {

    uint32_t version = 0;
    uint32_t current_version = (NVENCAPI_MAJOR_VERSION << 4) | NVENCAPI_MINOR_VERSION;
    NvEncodeAPIGetMaxSupportedVersion(&version);
    if (current_version > version) {
        LOG_CRITICAL("Current Driver Version does not support this NvEncodeAPI version, please upgrade driver");
        return false;
    }

    nvenc_fns = { NV_ENCODE_API_FUNCTION_LIST_VER };
    NvEncodeAPICreateInstance(&nvenc_fns);
    return true;
}

NvEncoder::~NvEncoder() {
    DestroyHWEncoder();
}

bool NvEncoder::CreateDefaultEncoderParams(NV_ENC_INITIALIZE_PARAMS* init_params, GUID codec_guid, GUID preset_guid, NV_ENC_TUNING_INFO tuning_info) {
    if (!encoder_ptr) {
        LOG_CRITICAL("Encoder Initialization failed");
        return false;
    }

    if (init_params == nullptr || init_params->encodeConfig == nullptr) {
        LOG_CRITICAL("pInitializeParams and pInitializeParams->encodeConfig can't be NULL");
        return false;
    }

    memset(init_params->encodeConfig, 0, sizeof(NV_ENC_CONFIG));
    auto pEncodeConfig = init_params->encodeConfig;
    memset(init_params, 0, sizeof(NV_ENC_INITIALIZE_PARAMS));
    init_params->encodeConfig = pEncodeConfig;


    init_params->encodeConfig->version = NV_ENC_CONFIG_VER;
    init_params->version = NV_ENC_INITIALIZE_PARAMS_VER;

    init_params->encodeGUID = codec_guid;
    init_params->presetGUID = preset_guid;
    init_params->encodeWidth = width;
    init_params->encodeHeight = height;
    init_params->darWidth = width;
    init_params->darHeight = height;
    init_params->frameRateNum = 60;
    init_params->frameRateDen = 1;
    init_params->enablePTD = 1;
    init_params->reportSliceOffsets = 0;
    init_params->enableSubFrameWrite = 0;
    init_params->maxEncodeWidth = width;
    init_params->maxEncodeHeight = height;
    init_params->enableMEOnlyMode = false;
    init_params->enableOutputInVidmem = false;
    
    init_params->enableEncodeAsync = 0;

    NV_ENC_PRESET_CONFIG preset_config = { NV_ENC_PRESET_CONFIG_VER, { NV_ENC_CONFIG_VER } };
    nvenc_fns.nvEncGetEncodePresetConfig(encoder_ptr, codec_guid, preset_guid, &preset_config);
    memcpy(init_params->encodeConfig, &preset_config.presetCfg, sizeof(NV_ENC_CONFIG));
    init_params->encodeConfig->frameIntervalP = 1;
    init_params->encodeConfig->gopLength = NVENC_INFINITE_GOPLENGTH;

    init_params->encodeConfig->rcParams.rateControlMode = NV_ENC_PARAMS_RC_CONSTQP;

    init_params->tuningInfo = tuning_info;
    preset_config = { NV_ENC_PRESET_CONFIG_VER, { NV_ENC_CONFIG_VER } };
    nvenc_fns.nvEncGetEncodePresetConfigEx(encoder_ptr, codec_guid, preset_guid, tuning_info, &preset_config);
    memcpy(init_params->encodeConfig, &preset_config.presetCfg, sizeof(NV_ENC_CONFIG));
    
     if (buffer_fmt == NV_ENC_BUFFER_FORMAT_YUV444 || buffer_fmt == NV_ENC_BUFFER_FORMAT_YUV444_10BIT) {
         init_params->encodeConfig->encodeCodecConfig.h264Config.chromaFormatIDC = 3;
     }
     init_params->encodeConfig->encodeCodecConfig.h264Config.idrPeriod = init_params->encodeConfig->gopLength;
    //init_params->encodeConfig->encodeCodecConfig.hevcConfig.pixelBitDepthMinus8 =
    //    (buffer_fmt == NV_ENC_BUFFER_FORMAT_YUV420_10BIT || buffer_fmt == NV_ENC_BUFFER_FORMAT_YUV444_10BIT ) ? 2 : 0;
    //if (buffer_fmt == NV_ENC_BUFFER_FORMAT_YUV444 || buffer_fmt == NV_ENC_BUFFER_FORMAT_YUV444_10BIT)
    //{
    //    init_params->encodeConfig->encodeCodecConfig.hevcConfig.chromaFormatIDC = 3;
    //}
    //init_params->encodeConfig->encodeCodecConfig.hevcConfig.idrPeriod = init_params->encodeConfig->gopLength;
    return true;
}

bool NvEncoder::CreateEncoder(const NV_ENC_INITIALIZE_PARAMS* encoder_params) {
    if (!encoder_ptr) {
        LOG_CRITICAL("Encoder Initialization failed");
        return false;
    }
    if (!encoder_params) {
        LOG_CRITICAL("Invalid NV_ENC_INITIALIZE_PARAMS ptr");
        return false;
    }
    if (encoder_params->encodeWidth == 0 || encoder_params->encodeHeight == 0) {
        LOG_CRITICAL("Invalid encoder width and height");
        return false;
    }
    if (encoder_params->encodeGUID != NV_ENC_CODEC_H264_GUID) {
        LOG_CRITICAL("Invalid codec guid");
        return false;
    }
    if (buffer_fmt == NV_ENC_BUFFER_FORMAT_YUV420_10BIT || buffer_fmt == NV_ENC_BUFFER_FORMAT_YUV444_10BIT) {
        LOG_CRITICAL("10-bit format isn't supported by H264 encoder");
        return false;
    }
    if ((buffer_fmt == NV_ENC_BUFFER_FORMAT_YUV444) &&
        (encoder_params->encodeConfig->encodeCodecConfig.h264Config.chromaFormatIDC != 3)) {
        LOG_CRITICAL("Invalid ChromaFormatIDC");
        return false;
    }

    memcpy(&init_params, encoder_params, sizeof(init_params));
    init_params.version = NV_ENC_INITIALIZE_PARAMS_VER;

    if (encoder_params->encodeConfig) {
        memcpy(&encode_cfg, encoder_params->encodeConfig, sizeof(encode_cfg));
        encode_cfg.version = NV_ENC_CONFIG_VER;
    } else {
        NV_ENC_PRESET_CONFIG preset_config = { NV_ENC_PRESET_CONFIG_VER, { NV_ENC_CONFIG_VER } };
            nvenc_fns.nvEncGetEncodePresetConfigEx(encoder_ptr, encoder_params->encodeGUID, encoder_params->presetGUID, encoder_params->tuningInfo, &preset_config);
            memcpy(&encode_cfg, &preset_config.presetCfg, sizeof(NV_ENC_CONFIG));
    }
    init_params.encodeConfig = &encode_cfg;

    nvenc_fns.nvEncInitializeEncoder(encoder_ptr, &init_params);

    encoder_initialized = true;
    width = init_params.encodeWidth;
    height = init_params.encodeHeight;
    max_enc_width = init_params.maxEncodeWidth;
    max_enc_height = init_params.maxEncodeHeight;

    for (size_t i = 0; i < completion_events.size(); ++i) {
        completion_events[i] = CreateEvent(NULL, FALSE, FALSE, NULL);
        NV_ENC_EVENT_PARAMS event_params = { NV_ENC_EVENT_PARAMS_VER };
        event_params.completionEvent = completion_events[i];
        nvenc_fns.nvEncRegisterAsyncEvent(encoder_ptr, &event_params);
    }

    InitializeBitstreamBuffer();
    RegisterInputResources();

    return true;
}

void NvEncoder::DestroyEncoder() {
    if (!encoder_ptr) {
        return;
    }
    UnregisterInputResources();
    DestroyHWEncoder();
}

void NvEncoder::DestroyHWEncoder() {
    if (!encoder_ptr) {
        return;
    }

    for (uint32_t i = 0; i < completion_events.size(); i++) {
        if (completion_events[i]) {
            NV_ENC_EVENT_PARAMS event_params = { NV_ENC_EVENT_PARAMS_VER };
            event_params.completionEvent = completion_events[i];
            nvenc_fns.nvEncUnregisterAsyncEvent(encoder_ptr, &event_params);
            CloseHandle(completion_events[i]);
        }
    }
    completion_events.fill(nullptr);

    DestroyBitstreamBuffer();

    nvenc_fns.nvEncDestroyEncoder(encoder_ptr);
    encoder_ptr = nullptr;
    encoder_initialized = false;
}

NvEncInputFrame* NvEncoder::GetActiveFrame() {
    return &input_frames[active_input_frame];
}

NvEncInputFrame* NvEncoder::GetStagingFrame() {
    return &input_frames[(active_input_frame + 1) % 2];
}

void NvEncoder::MapResources() {
    NV_ENC_MAP_INPUT_RESOURCE map_input_resources = { NV_ENC_MAP_INPUT_RESOURCE_VER };

    map_input_resources.registeredResource = registered_resources[active_input_frame];
    (nvenc_fns.nvEncMapInputResource(encoder_ptr, &map_input_resources));
    mapped_input_buffers[active_input_frame] = map_input_resources.mappedResource;
}

void NvEncoder::EncodeActiveFrame(std::vector<std::vector<uint8_t>> &packets_out, NV_ENC_PIC_PARAMS *pic_params) {
    //LOG_INFO("Encoding on frame idx {}", active_input_frame);

    packets_out.clear();
    if (!encoder_initialized) {
        LOG_CRITICAL("Encoder device not found");
        return;
    }
    MapResources();
    auto start_timer = std::chrono::system_clock::now();
    NVENCSTATUS status = DoEncode(mapped_input_buffers[active_input_frame],
        bitstream_output[active_input_frame], pic_params);
    auto e1 = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - start_timer);
    //LOG_INFO("Encode time?: {}", e1.count());
    if (status == NV_ENC_SUCCESS || status == NV_ENC_ERR_NEED_MORE_INPUT) {
        GetEncodedPacket(packets_out);
    } else {
        LOG_CRITICAL("nvEncEncodePicture API failed");
    }
}

void NvEncoder::GetSequenceParams(std::vector<uint8_t> &seq_params) {
    uint8_t spspps_data[1024]; // Assume maximum spspps data is 1KB or less
    memset(spspps_data, 0, sizeof(spspps_data));
    NV_ENC_SEQUENCE_PARAM_PAYLOAD payload = { NV_ENC_SEQUENCE_PARAM_PAYLOAD_VER };
    uint32_t spspps_size = 0;

    payload.spsppsBuffer = spspps_data;
    payload.inBufferSize = sizeof(spspps_data);
    payload.outSPSPPSPayloadSize = &spspps_size;
    (nvenc_fns.nvEncGetSequenceParams(encoder_ptr, &payload));

    seq_params.clear();
    seq_params.insert(seq_params.end(), &spspps_data[0], &spspps_data[spspps_size]);
}

NVENCSTATUS NvEncoder::DoEncode(NV_ENC_INPUT_PTR input_buffer, NV_ENC_OUTPUT_PTR output_buffer, NV_ENC_PIC_PARAMS *in_params) {
    NV_ENC_PIC_PARAMS pic_params = {};
    if (in_params != nullptr) {
        pic_params = *in_params;
    }
    pic_params.version = NV_ENC_PIC_PARAMS_VER;
    pic_params.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
    pic_params.inputBuffer = input_buffer;
    pic_params.bufferFmt = GetPixelFormat();
    pic_params.inputWidth = GetEncodeWidth();
    pic_params.inputHeight = GetEncodeHeight();
    pic_params.outputBitstream = output_buffer;
    pic_params.completionEvent = GetCompletionEvent();
    NVENCSTATUS nvStatus = nvenc_fns.nvEncEncodePicture(encoder_ptr, &pic_params);

    return nvStatus; 
}

void NvEncoder::SendEOS() {
    NV_ENC_PIC_PARAMS picParams = { NV_ENC_PIC_PARAMS_VER };
    picParams.encodePicFlags = NV_ENC_PIC_FLAG_EOS;
    picParams.completionEvent = GetCompletionEvent();
    nvenc_fns.nvEncEncodePicture(encoder_ptr, &picParams);
}

void NvEncoder::EndEncode(std::vector<std::vector<uint8_t>> &out_packets) {
    out_packets.clear();
    if (!encoder_initialized) {
        LOG_CRITICAL("Encoder device not initialized");
        return;
    }

    SendEOS();
    GetEncodedPacket(out_packets);
}

void NvEncoder::GetEncodedPacket(std::vector<std::vector<uint8_t>> &packets_out) {
    // WaitForCompletionEvent(active_input_frame);
    NV_ENC_LOCK_BITSTREAM lock_bitstream_data = { NV_ENC_LOCK_BITSTREAM_VER };
    lock_bitstream_data.outputBitstream = bitstream_output[active_input_frame];
    lock_bitstream_data.doNotWait = false;
    nvenc_fns.nvEncLockBitstream(encoder_ptr, &lock_bitstream_data);

    uint8_t* data_ptr = reinterpret_cast<uint8_t*>(lock_bitstream_data.bitstreamBufferPtr);
    packets_out.push_back({});
    packets_out[0].clear();
    packets_out[0].insert(packets_out[0].end(), &data_ptr[0], &data_ptr[lock_bitstream_data.bitstreamSizeInBytes]);

    nvenc_fns.nvEncUnlockBitstream(encoder_ptr, lock_bitstream_data.outputBitstream);

    if (mapped_input_buffers[active_input_frame]) {
        nvenc_fns.nvEncUnmapInputResource(encoder_ptr, mapped_input_buffers[active_input_frame]);
        mapped_input_buffers[active_input_frame] = nullptr;
    }
}

bool NvEncoder::Reconfigure(const NV_ENC_RECONFIGURE_PARAMS *reconfigure_params) {
    nvenc_fns.nvEncReconfigureEncoder(encoder_ptr, const_cast<NV_ENC_RECONFIGURE_PARAMS*>(reconfigure_params));

    memcpy(&init_params, &(reconfigure_params->reInitEncodeParams), sizeof(init_params));
    if (reconfigure_params->reInitEncodeParams.encodeConfig) {
        memcpy(&encode_cfg, reconfigure_params->reInitEncodeParams.encodeConfig, sizeof(encode_cfg));
    }

    width = init_params.encodeWidth;
    height = init_params.encodeHeight;
    max_enc_width = init_params.maxEncodeWidth;
    max_enc_height = init_params.maxEncodeHeight;

    return true;
}

NV_ENC_REGISTERED_PTR NvEncoder::RegisterResource(void *buf, int w, int h, int pitch, NV_ENC_BUFFER_FORMAT buffer_format) {
    NV_ENC_REGISTER_RESOURCE register_resource = { NV_ENC_REGISTER_RESOURCE_VER };
    register_resource.resourceType = NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX;
    register_resource.resourceToRegister = buf;
    register_resource.width = w;
    register_resource.height = h;
    register_resource.pitch = pitch;
    register_resource.bufferFormat = buffer_format;
    register_resource.bufferUsage = NV_ENC_INPUT_IMAGE;
    nvenc_fns.nvEncRegisterResource(encoder_ptr, &register_resource);

    return register_resource.registeredResource;
}

void NvEncoder::RegisterInputResources() {
    const int w = max_enc_width, h = max_enc_height, pitch = 0;

    ID3D11Texture2D* input_textures[2];
    for (int i = 0; i < 2; i++) {
        D3D11_TEXTURE2D_DESC desc;
        memset(&desc, 0, sizeof(D3D11_TEXTURE2D_DESC));
        desc.Width = w;
        desc.Height = h;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET;
        desc.CPUAccessFlags = 0;
        if (device->CreateTexture2D(&desc, nullptr, &input_textures[i]) != S_OK) {
            LOG_CRITICAL("Failed to create DirectX texture");
            return;
        }
        input_frames[i].input_ptr = input_textures[i];
    }
    
    for (int i = 0; i < 2; i++) {
        NV_ENC_REGISTERED_PTR registered_ptr = RegisterResource(input_textures[i], w, h, 0,
            NV_ENC_BUFFER_FORMAT_ARGB);
        
        std::vector<uint32_t> chroma_offsets;
        NvEncoder::GetChromaSubPlaneOffsets(buffer_fmt, pitch, height, chroma_offsets);
        NvEncInputFrame &input_frame = input_frames[i];

        input_frame.chroma_offsets[0] = 0;
        input_frame.chroma_offsets[1] = 0;
        for (uint32_t ch = 0; ch < chroma_offsets.size(); ch++) {
            input_frame.chroma_offsets[ch] = chroma_offsets[ch];
        }
        input_frame.num_chroma_planes = NvEncoder::GetNumChromaPlanes(buffer_fmt);
        input_frame.pitch = pitch;
        input_frame.chroma_pitch = NvEncoder::GetChromaPitch(buffer_fmt, pitch);
        input_frame.buffer_format = buffer_fmt;
        input_frame.resource_type = NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX;

        registered_resources[i] = registered_ptr;
    }
}

void NvEncoder::FlushEncoder() {
    std::vector<std::vector<uint8_t>> tmp;
    EndEncode(tmp);
}

void NvEncoder::UnregisterInputResources() {
    FlushEncoder();
    
    for (uint32_t i = 0; i < mapped_input_buffers.size(); ++i) {
        if (mapped_input_buffers[i]) {
            nvenc_fns.nvEncUnmapInputResource(encoder_ptr, mapped_input_buffers[i]);
        }
    }
    mapped_input_buffers.fill(nullptr);

    for (uint32_t i = 0; i < registered_resources.size(); ++i) {
        if (registered_resources[i]) {
            nvenc_fns.nvEncUnregisterResource(encoder_ptr, registered_resources[i]);
        }
    }
    registered_resources.fill(nullptr);

    // FREE DX
    for (size_t i = 0; i < input_frames.size(); ++i) {
        input_frames[i].input_ptr->Release();
    }
    input_frames.fill({ 0 });
    
    if (device_ctx != nullptr) {
        device_ctx->Release();
        device_ctx = nullptr;
    }

    if (device != nullptr) {
        device->Release();
        device = nullptr;
    }
}


void NvEncoder::WaitForCompletionEvent(int event_idx) {
    NV_ENC_CONFIG cfg = { 0 };
    NV_ENC_INITIALIZE_PARAMS params = { 0 };
    params.encodeConfig = &cfg;
    GetInitializeParams(&params);

    if (0U == params.enableEncodeAsync) {
        return;
    }
    if (WaitForSingleObject(completion_events[event_idx], 20000) == WAIT_FAILED) {
        LOG_CRITICAL("Failed to encode frame");
    }
}

uint32_t NvEncoder::GetWidthInBytes(const NV_ENC_BUFFER_FORMAT buffer_fmt, const uint32_t width) {
    switch (buffer_fmt) {
    case NV_ENC_BUFFER_FORMAT_NV12:
    case NV_ENC_BUFFER_FORMAT_YV12:
    case NV_ENC_BUFFER_FORMAT_IYUV:
    case NV_ENC_BUFFER_FORMAT_YUV444:
        return width;
    case NV_ENC_BUFFER_FORMAT_YUV420_10BIT:
    case NV_ENC_BUFFER_FORMAT_YUV444_10BIT:
        return width * 2;
    case NV_ENC_BUFFER_FORMAT_ARGB:
    case NV_ENC_BUFFER_FORMAT_ARGB10:
    case NV_ENC_BUFFER_FORMAT_AYUV:
    case NV_ENC_BUFFER_FORMAT_ABGR:
    case NV_ENC_BUFFER_FORMAT_ABGR10:
        return width * 4;
    default:
        LOG_CRITICAL("Invalid Buffer format");
        return 0;
    }
}

uint32_t NvEncoder::GetNumChromaPlanes(const NV_ENC_BUFFER_FORMAT buffer_fmt) {
    switch (buffer_fmt) {
    case NV_ENC_BUFFER_FORMAT_NV12:
    case NV_ENC_BUFFER_FORMAT_YUV420_10BIT:
        return 1;
    case NV_ENC_BUFFER_FORMAT_YV12:
    case NV_ENC_BUFFER_FORMAT_IYUV:
    case NV_ENC_BUFFER_FORMAT_YUV444:
    case NV_ENC_BUFFER_FORMAT_YUV444_10BIT:
        return 2;
    case NV_ENC_BUFFER_FORMAT_ARGB:
    case NV_ENC_BUFFER_FORMAT_ARGB10:
    case NV_ENC_BUFFER_FORMAT_AYUV:
    case NV_ENC_BUFFER_FORMAT_ABGR:
    case NV_ENC_BUFFER_FORMAT_ABGR10:
        return 0;
    default:
        LOG_CRITICAL("Invalid Buffer format");
        return -1;
    }
}

uint32_t NvEncoder::GetChromaPitch(const NV_ENC_BUFFER_FORMAT buffer_fmt,const uint32_t luma_pitch) {
    switch (buffer_fmt) {
    case NV_ENC_BUFFER_FORMAT_NV12:
    case NV_ENC_BUFFER_FORMAT_YUV420_10BIT:
    case NV_ENC_BUFFER_FORMAT_YUV444:
    case NV_ENC_BUFFER_FORMAT_YUV444_10BIT:
        return luma_pitch;
    case NV_ENC_BUFFER_FORMAT_YV12:
    case NV_ENC_BUFFER_FORMAT_IYUV:
        return (luma_pitch + 1) / 2;
    case NV_ENC_BUFFER_FORMAT_ARGB:
    case NV_ENC_BUFFER_FORMAT_ARGB10:
    case NV_ENC_BUFFER_FORMAT_AYUV:
    case NV_ENC_BUFFER_FORMAT_ABGR:
    case NV_ENC_BUFFER_FORMAT_ABGR10:
        return 0;
    default:
        LOG_CRITICAL("Invalid Buffer format");
        return -1;
    }
}

void NvEncoder::GetChromaSubPlaneOffsets(const NV_ENC_BUFFER_FORMAT buffer_fmt, const uint32_t pitch, 
        const uint32_t height, std::vector<uint32_t>& chroma_offsets_out) {
    chroma_offsets_out.clear();
    switch (buffer_fmt) {
    case NV_ENC_BUFFER_FORMAT_NV12:
    case NV_ENC_BUFFER_FORMAT_YUV420_10BIT:
        chroma_offsets_out.push_back(pitch * height);
        return;
    case NV_ENC_BUFFER_FORMAT_YV12:
    case NV_ENC_BUFFER_FORMAT_IYUV:
        chroma_offsets_out.push_back(pitch * height);
        chroma_offsets_out.push_back(chroma_offsets_out[0] + 
            (NvEncoder::GetChromaPitch(buffer_fmt, pitch) * GetChromaHeight(buffer_fmt, height)));
        return;
    case NV_ENC_BUFFER_FORMAT_YUV444:
    case NV_ENC_BUFFER_FORMAT_YUV444_10BIT:
        chroma_offsets_out.push_back(pitch * height);
        chroma_offsets_out.push_back(chroma_offsets_out[0] + (pitch * height));
        return;
    case NV_ENC_BUFFER_FORMAT_ARGB:
    case NV_ENC_BUFFER_FORMAT_ARGB10:
    case NV_ENC_BUFFER_FORMAT_AYUV:
    case NV_ENC_BUFFER_FORMAT_ABGR:
    case NV_ENC_BUFFER_FORMAT_ABGR10:
        return;
    default:
        LOG_CRITICAL("Invalid Buffer format");
        return;
    }
}

uint32_t NvEncoder::GetChromaHeight(const NV_ENC_BUFFER_FORMAT buffer_fmt, const uint32_t luma_height) {
    switch (buffer_fmt) {
    case NV_ENC_BUFFER_FORMAT_YV12:
    case NV_ENC_BUFFER_FORMAT_IYUV:
    case NV_ENC_BUFFER_FORMAT_NV12:
    case NV_ENC_BUFFER_FORMAT_YUV420_10BIT:
        return (luma_height + 1) / 2;
    case NV_ENC_BUFFER_FORMAT_YUV444:
    case NV_ENC_BUFFER_FORMAT_YUV444_10BIT:
        return luma_height;
    case NV_ENC_BUFFER_FORMAT_ARGB:
    case NV_ENC_BUFFER_FORMAT_ARGB10:
    case NV_ENC_BUFFER_FORMAT_AYUV:
    case NV_ENC_BUFFER_FORMAT_ABGR:
    case NV_ENC_BUFFER_FORMAT_ABGR10:
        return 0;
    default:
        LOG_CRITICAL("Invalid Buffer format");
        return 0;
    }
}

uint32_t NvEncoder::GetChromaWidthInBytes(const NV_ENC_BUFFER_FORMAT buffer_fmt, const uint32_t luma_width) {
    switch (buffer_fmt) {
    case NV_ENC_BUFFER_FORMAT_YV12:
    case NV_ENC_BUFFER_FORMAT_IYUV:
        return (luma_width + 1) / 2;
    case NV_ENC_BUFFER_FORMAT_NV12:
        return luma_width;
    case NV_ENC_BUFFER_FORMAT_YUV420_10BIT:
        return 2 * luma_width;
    case NV_ENC_BUFFER_FORMAT_YUV444:
        return luma_width;
    case NV_ENC_BUFFER_FORMAT_YUV444_10BIT:
        return 2 * luma_width;
    case NV_ENC_BUFFER_FORMAT_ARGB:
    case NV_ENC_BUFFER_FORMAT_ARGB10:
    case NV_ENC_BUFFER_FORMAT_AYUV:
    case NV_ENC_BUFFER_FORMAT_ABGR:
    case NV_ENC_BUFFER_FORMAT_ABGR10:
        return 0;
    default:
        LOG_CRITICAL("Invalid Buffer format");
        return 0;
    }
}


int NvEncoder::GetCapabilityValue(GUID guid_codec, NV_ENC_CAPS caps_to_query) {
    if (!encoder_ptr) {
        return 0;
    }
    NV_ENC_CAPS_PARAM caps_param = { NV_ENC_CAPS_PARAM_VER };
    caps_param.capsToQuery = caps_to_query;
    int v;
    nvenc_fns.nvEncGetEncodeCaps(encoder_ptr, guid_codec, &caps_param, &v);
    return v;
}

int NvEncoder::GetFrameSize() const {
    switch (GetPixelFormat()) {
    case NV_ENC_BUFFER_FORMAT_YV12:
    case NV_ENC_BUFFER_FORMAT_IYUV:
    case NV_ENC_BUFFER_FORMAT_NV12:
        return GetEncodeWidth() * (GetEncodeHeight() + (GetEncodeHeight() + 1) / 2);
    case NV_ENC_BUFFER_FORMAT_YUV420_10BIT:
        return 2 * GetEncodeWidth() * (GetEncodeHeight() + (GetEncodeHeight() + 1) / 2);
    case NV_ENC_BUFFER_FORMAT_YUV444:
        return GetEncodeWidth() * GetEncodeHeight() * 3;
    case NV_ENC_BUFFER_FORMAT_YUV444_10BIT:
        return 2 * GetEncodeWidth() * GetEncodeHeight() * 3;
    case NV_ENC_BUFFER_FORMAT_ARGB:
    case NV_ENC_BUFFER_FORMAT_ARGB10:
    case NV_ENC_BUFFER_FORMAT_AYUV:
    case NV_ENC_BUFFER_FORMAT_ABGR:
    case NV_ENC_BUFFER_FORMAT_ABGR10:
        return 4 * GetEncodeWidth() * GetEncodeHeight();
    default:
        LOG_CRITICAL("Invalid Buffer format");
        return 0;
    }
}

void NvEncoder::GetInitializeParams(NV_ENC_INITIALIZE_PARAMS* out_initialize_params) {
    if (!out_initialize_params || !out_initialize_params->encodeConfig) {
        LOG_CRITICAL("Both pInitializeParams and pInitializeParams->encodeConfig can't be NULL");
        return;
    }
    NV_ENC_CONFIG *out_encode_config = init_params.encodeConfig;
    *out_encode_config = encode_cfg;
    *out_initialize_params = init_params;
    out_initialize_params->encodeConfig = out_encode_config;
}

void NvEncoder::InitializeBitstreamBuffer() {
    for (int i = 0; i < bitstream_output.size(); i++) {
        NV_ENC_CREATE_BITSTREAM_BUFFER create_bitstream_buffer = { NV_ENC_CREATE_BITSTREAM_BUFFER_VER };
        nvenc_fns.nvEncCreateBitstreamBuffer(encoder_ptr, &create_bitstream_buffer);
        bitstream_output[i] = create_bitstream_buffer.bitstreamBuffer;
    }
}

void NvEncoder::DestroyBitstreamBuffer() {
    for (uint32_t i = 0; i < bitstream_output.size(); i++) {
        if (bitstream_output[i]) {
            nvenc_fns.nvEncDestroyBitstreamBuffer(encoder_ptr, bitstream_output[i]);
        }
    }

    bitstream_output.fill(nullptr);
}