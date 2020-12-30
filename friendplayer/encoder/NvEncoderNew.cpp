#include "encoder/NvEncoderNew.h"

#include "common/Log.h"

NvEncoderNew::NvEncoderNew(ID3D11Device* device, uint32_t width, uint32_t height, NV_ENC_BUFFER_FORMAT buffer_fmt) :
    device(device), 
    dev_type(NV_ENC_DEVICE_TYPE_DIRECTX),
    width(width),
    height(height),
    max_enc_width(width),
    max_enc_height(height),
    buffer_fmt(buffer_fmt) {
    
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

bool NvEncoderNew::LoadNvEncApi() {

    uint32_t version = 0;
    uint32_t current_version = (NVENCAPI_MAJOR_VERSION << 4) | NVENCAPI_MINOR_VERSION;
    NvEncodeAPIGetMaxSupportedVersion(&version);
    if (current_version > version) {
        LOG_CRITICAL("Current Driver Version does not support this NvEncodeAPI version, please upgrade driver");
        return false;
    }

    nvenc_fns = { NV_ENCODE_API_FUNCTION_LIST_VER };
    NvEncodeAPICreateInstance(&nvenc_fns);
}

NvEncoderNew::~NvEncoderNew() {
    DestroyHWEncoder();
}

bool NvEncoderNew::CreateDefaultEncoderParams(NV_ENC_INITIALIZE_PARAMS* init_params, GUID codec_guid, GUID preset_guid, NV_ENC_TUNING_INFO tuning_info = NV_ENC_TUNING_INFO_UNDEFINED) {
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
    
    // EnableOutputInVidmem!!
    init_params->enableEncodeAsync = GetCapabilityValue(codec_guid, NV_ENC_CAPS_ASYNC_ENCODE_SUPPORT);

    NV_ENC_PRESET_CONFIG preset_config = { NV_ENC_PRESET_CONFIG_VER, { NV_ENC_CONFIG_VER } };
    nvenc_fns.nvEncGetEncodePresetConfig(encoder_ptr, codec_guid, preset_guid, &preset_config);
    memcpy(init_params->encodeConfig, &preset_config.presetCfg, sizeof(NV_ENC_CONFIG));
    init_params->encodeConfig->frameIntervalP = 1;
    init_params->encodeConfig->gopLength = NVENC_INFINITE_GOPLENGTH;

    init_params->encodeConfig->rcParams.rateControlMode = NV_ENC_PARAMS_RC_CONSTQP;

    init_params->tuningInfo = tuning_info;
    NV_ENC_PRESET_CONFIG preset_config = { NV_ENC_PRESET_CONFIG_VER, { NV_ENC_CONFIG_VER } };
    nvenc_fns.nvEncGetEncodePresetConfigEx(encoder_ptr, codec_guid, preset_guid, tuning_info, &preset_config);
    memcpy(init_params->encodeConfig, &preset_config.presetCfg, sizeof(NV_ENC_CONFIG));
    
    if (buffer_fmt == NV_ENC_BUFFER_FORMAT_YUV444 || buffer_fmt == NV_ENC_BUFFER_FORMAT_YUV444_10BIT) {
        init_params->encodeConfig->encodeCodecConfig.h264Config.chromaFormatIDC = 3;
    }
    init_params->encodeConfig->encodeCodecConfig.h264Config.idrPeriod = init_params->encodeConfig->gopLength;
    return true;
}

bool NvEncoderNew::CreateEncoder(const NV_ENC_INITIALIZE_PARAMS* encoder_params) {
    if (!encoder_ptr) {
        LOG_CRITICAL("Encoder Initialization failed");
        return false;
    }
    if (!encoder_params) {
        LOG_CRITICAL("Invalid NV_ENC_INITIALIZE_PARAMS ptr");
        return false
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
    
    m_vMappedInputBuffers.resize(m_nEncoderBuffer, nullptr);

//    if (!m_bOutputInVideoMemory)
        m_vpCompletionEvent.resize(m_nEncoderBuffer, nullptr);

    for (uint32_t i = 0; i < m_vpCompletionEvent.size(); i++) {
        m_vpCompletionEvent[i] = CreateEvent(NULL, FALSE, FALSE, NULL);
        NV_ENC_EVENT_PARAMS event_params = { NV_ENC_EVENT_PARAMS_VER };
        event_params.completionEvent = m_vpCompletionEvent[i];
        nvenc_fns.nvEncRegisterAsyncEvent(encoder_ptr, &event_params);
    }

//    if (!m_bOutputInVideoMemory)
        m_vBitstreamOutputBuffer.resize(m_nEncoderBuffer, nullptr);
        InitializeBitstreamBuffer();

    AllocateInputBuffers(m_nEncoderBuffer);
}

void NvEncoderNew::DestroyEncoder() {
    if (!encoder_ptr) {
        return;
    }
    ReleaseInputBuffers();
    DestroyHWEncoder();
}

void NvEncoderNew::DestroyHWEncoder() {
    if (!encoder_ptr) {
        return;
    }

    for (uint32_t i = 0; i < m_vpCompletionEvent.size(); i++) {
        if (m_vpCompletionEvent[i]) {
            NV_ENC_EVENT_PARAMS event_params = { NV_ENC_EVENT_PARAMS_VER };
            event_params.completionEvent = m_vpCompletionEvent[i];
            nvenc_fns.nvEncUnregisterAsyncEvent(encoder_ptr, &event_params);
            CloseHandle(m_vpCompletionEvent[i]);
        }
    }

    m_vpCompletionEvent.clear();
    DestroyBitstreamBuffer();
    nvenc_fns.nvEncDestroyEncoder(encoder_ptr);
    encoder_ptr = nullptr;
    encoder_initialized = false;
}

const NvEncInputFrame* NvEncoderNew::GetActiveFrame() {
    return &input_frames[active_input_frame];
}

const NvEncInputFrame* NvEncoderNew::GetStagingFrame() {
    return &input_frames[(active_input_frame + 1) % 2];
}

void NvEncoderNew::MapResources() {
    NV_ENC_MAP_INPUT_RESOURCE map_input_resources = { NV_ENC_MAP_INPUT_RESOURCE_VER };

    map_input_resources.registeredResource = m_vRegisteredResources[active_input_frame];
    (nvenc_fns.nvEncMapInputResource(encoder_ptr, &map_input_resources));
    m_vMappedInputBuffers[active_input_frame] = map_input_resources.mappedResource;
}

void NvEncoderNew::EncodeActiveFrame(std::vector<std::vector<uint8_t>> &packets_out, NV_ENC_PIC_PARAMS *pic_params) {
    packets_out.clear();
    if (!encoder_initialized) {
        LOG_CRITICAL("Encoder device not found");
        return;
    }
    MapResources();
    NVENCSTATUS status = DoEncode(m_vMappedInputBuffers[bfrIdx], m_vBitstreamOutputBuffer[bfrIdx], pPicParams);

    if (nvStatus == NV_ENC_SUCCESS || nvStatus == NV_ENC_ERR_NEED_MORE_INPUT)
    {
        m_iToSend++;
        GetEncodedPacket(m_vBitstreamOutputBuffer, vPacket, true);
    }
    else
    {
        LOG_CRITICAL("nvEncEncodePicture API failed", nvStatus);
    }
}

void NvEncoderNew::RunMotionEstimation(std::vector<uint8_t> &mvData)
{
    if (!encoder_ptr)
    {
        LOG_CRITICAL("Encoder Initialization failed", NV_ENC_ERR_NO_ENCODE_DEVICE);
        return;
    }

    const uint32_t bfrIdx = m_iToSend % m_nEncoderBuffer;

    MapResources(bfrIdx);

    NVENCSTATUS nvStatus = DoMotionEstimation(m_vMappedInputBuffers[bfrIdx], m_vMappedRefBuffers[bfrIdx], m_vMVDataOutputBuffer[bfrIdx]);

    if (nvStatus == NV_ENC_SUCCESS)
    {
        m_iToSend++;
        std::vector<std::vector<uint8_t>> vPacket;
        GetEncodedPacket(m_vMVDataOutputBuffer, vPacket, true);
        if (vPacket.size() != 1)
        {
            LOG_CRITICAL("GetEncodedPacket() doesn't return one (and only one) MVData", NV_ENC_ERR_GENERIC);
        }
        mvData = vPacket[0];
    }
    else
    {
        LOG_CRITICAL("nvEncEncodePicture API failed", nvStatus);
    }
}


void NvEncoderNew::GetSequenceParams(std::vector<uint8_t> &seqParams)
{
    uint8_t spsppsData[1024]; // Assume maximum spspps data is 1KB or less
    memset(spsppsData, 0, sizeof(spsppsData));
    NV_ENC_SEQUENCE_PARAM_PAYLOAD payload = { NV_ENC_SEQUENCE_PARAM_PAYLOAD_VER };
    uint32_t spsppsSize = 0;

    payload.spsppsBuffer = spsppsData;
    payload.inBufferSize = sizeof(spsppsData);
    payload.outSPSPPSPayloadSize = &spsppsSize;
    (nvenc_fns.nvEncGetSequenceParams(encoder_ptr, &payload));
    seqParams.clear();
    seqParams.insert(seqParams.end(), &spsppsData[0], &spsppsData[spsppsSize]);
}

NVENCSTATUS NvEncoderNew::DoEncode(NV_ENC_INPUT_PTR input_buffer, NV_ENC_OUTPUT_PTR output_buffer, NV_ENC_PIC_PARAMS *in_params) {
{
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
    NVENCSTATUS nvStatus = nvenc_fns.nvEncEncodePicture(encoder_ptr, &picParams);

    return nvStatus; 
}

void NvEncoderNew::SendEOS()
{
    NV_ENC_PIC_PARAMS picParams = { NV_ENC_PIC_PARAMS_VER };
    picParams.encodePicFlags = NV_ENC_PIC_FLAG_EOS;
    picParams.completionEvent = GetCompletionEvent(m_iToSend % m_nEncoderBuffer);
    (nvenc_fns.nvEncEncodePicture(encoder_ptr, &picParams));
}

void NvEncoderNew::EndEncode(std::vector<std::vector<uint8_t>> &vPacket)
{
    vPacket.clear();
    if (!IsHWEncoderInitialized())
    {
        LOG_CRITICAL("Encoder device not initialized", NV_ENC_ERR_ENCODER_NOT_INITIALIZED);
    }

    SendEOS();

    GetEncodedPacket(m_vBitstreamOutputBuffer, vPacket, false);
}

void NvEncoderNew::GetEncodedPacket(std::vector<NV_ENC_OUTPUT_PTR> &vOutputBuffer, std::vector<std::vector<uint8_t>> &vPacket, bool bOutputDelay)
{
    unsigned i = 0;
    int iEnd = bOutputDelay ? m_iToSend - m_nOutputDelay : m_iToSend;
    for (; m_iGot < iEnd; m_iGot++)
    {
        WaitForCompletionEvent(m_iGot % m_nEncoderBuffer);
        NV_ENC_LOCK_BITSTREAM lockBitstreamData = { NV_ENC_LOCK_BITSTREAM_VER };
        lockBitstreamData.outputBitstream = vOutputBuffer[m_iGot % m_nEncoderBuffer];
        lockBitstreamData.doNotWait = false;
        (nvenc_fns.nvEncLockBitstream(encoder_ptr, &lockBitstreamData));
  
        uint8_t *pData = (uint8_t *)lockBitstreamData.bitstreamBufferPtr;
        if (vPacket.size() < i + 1)
        {
            vPacket.push_back(std::vector<uint8_t>());
        }
        vPacket[i].clear();
        vPacket[i].insert(vPacket[i].end(), &pData[0], &pData[lockBitstreamData.bitstreamSizeInBytes]);
        i++;

        (nvenc_fns.nvEncUnlockBitstream(encoder_ptr, lockBitstreamData.outputBitstream));

        if (m_vMappedInputBuffers[m_iGot % m_nEncoderBuffer])
        {
            (nvenc_fns.nvEncUnmapInputResource(encoder_ptr, m_vMappedInputBuffers[m_iGot % m_nEncoderBuffer]));
            m_vMappedInputBuffers[m_iGot % m_nEncoderBuffer] = nullptr;
        }

        if (m_bMotionEstimationOnly && m_vMappedRefBuffers[m_iGot % m_nEncoderBuffer])
        {
            (nvenc_fns.nvEncUnmapInputResource(encoder_ptr, m_vMappedRefBuffers[m_iGot % m_nEncoderBuffer]));
            m_vMappedRefBuffers[m_iGot % m_nEncoderBuffer] = nullptr;
        }
    }
}

bool NvEncoderNew::Reconfigure(const NV_ENC_RECONFIGURE_PARAMS *pReconfigureParams)
{
    (nvenc_fns.nvEncReconfigureEncoder(encoder_ptr, const_cast<NV_ENC_RECONFIGURE_PARAMS*>(pReconfigureParams)));

    memcpy(&init_params, &(pReconfigureParams->reInitEncodeParams), sizeof(init_params));
    if (pReconfigureParams->reInitEncodeParams.encodeConfig)
    {
        memcpy(&encode_cfg, pReconfigureParams->reInitEncodeParams.encodeConfig, sizeof(encode_cfg));
    }

    width = init_params.encodeWidth;
    height = init_params.encodeHeight;
    m_nMaxEncodeWidth = init_params.maxEncodeWidth;
    m_nMaxEncodeHeight = init_params.maxEncodeHeight;

    return true;
}

NV_ENC_REGISTERED_PTR NvEncoderNew::RegisterResource(void *pBuffer, NV_ENC_INPUT_RESOURCE_TYPE eResourceType,
    int width, int height, int pitch, NV_ENC_BUFFER_FORMAT bufferFormat, NV_ENC_BUFFER_USAGE bufferUsage)
{
    NV_ENC_REGISTER_RESOURCE registerResource = { NV_ENC_REGISTER_RESOURCE_VER };
    registerResource.resourceType = eResourceType;
    registerResource.resourceToRegister = pBuffer;
    registerResource.width = width;
    registerResource.height = height;
    registerResource.pitch = pitch;
    registerResource.bufferFormat = bufferFormat;
    registerResource.bufferUsage = bufferUsage;
    (nvenc_fns.nvEncRegisterResource(encoder_ptr, &registerResource));

    return registerResource.registeredResource;
}

void NvEncoderNew::RegisterInputResources(NV_ENC_INPUT_RESOURCE_TYPE resource_type, int width, int height,
                                          int pitch, NV_ENC_BUFFER_FORMAT buffer_fmt) {
    ID3D11Texture2D* input_textures[2];
    D3D11_TEXTURE2D_DESC desc;
    memset(&desc, 0, sizeof(D3D11_TEXTURE2D_DESC));
    desc.Width = max_enc_width;
    desc.Height = max_enc_height;
    desc.MipLevels = 1;
    

    NV_ENC_REGISTERED_PTR registered_ptr = RegisterResource(inputframes[i], eResourceType, width, height, pitch, bufferFormat, NV_ENC_INPUT_IMAGE);
    
    std::vector<uint32_t> _chromaOffsets;
    NvEncoderNew::GetChromaSubPlaneOffsets(bufferFormat, pitch, height, _chromaOffsets);
    NvEncInputFrame input_frame = {};
    input_frame.inputPtr = (void *)inputframes[i];
    input_frame.chromaOffsets[0] = 0;
    input_frame.chromaOffsets[1] = 0;
    for (uint32_t ch = 0; ch < _chromaOffsets.size(); ch++) {
        inputframe.chromaOffsets[ch] = _chromaOffsets[ch];
    }
    input_frame.numChromaPlanes = NvEncoderNew::GetNumChromaPlanes(bufferFormat);
    input_frame.pitch = pitch;
    input_frame.chromaPitch = NvEncoderNew::GetChromaPitch(bufferFormat, pitch);
    input_frame.bufferFormat = bufferFormat;
    input_frame.resourceType = eResourceType;

    m_vRegisteredResources.push_back(registeredPtr);
    m_vInputFrames.push_back(inputframe);
    
}

void NvEncoderNew::FlushEncoder()
{
    if (!m_bMotionEstimationOnly && !m_bOutputInVideoMemory)
    {
        // Incase of error it is possible for buffers still mapped to encoder.
        // flush the encoder queue and then unmapped it if any surface is still mapped
        try
        {
            std::vector<std::vector<uint8_t>> vPacket;
            EndEncode(vPacket);
        }
        catch (...)
        {

        }
    }
}

void NvEncoderNew::UnregisterInputResources()
{
    FlushEncoder();
    
    if (m_bMotionEstimationOnly)
    {
        for (uint32_t i = 0; i < m_vMappedRefBuffers.size(); ++i)
        {
            if (m_vMappedRefBuffers[i])
            {
                nvenc_fns.nvEncUnmapInputResource(encoder_ptr, m_vMappedRefBuffers[i]);
            }
        }
    }
    m_vMappedRefBuffers.clear();

    for (uint32_t i = 0; i < m_vMappedInputBuffers.size(); ++i)
    {
        if (m_vMappedInputBuffers[i])
        {
            nvenc_fns.nvEncUnmapInputResource(encoder_ptr, m_vMappedInputBuffers[i]);
        }
    }
    m_vMappedInputBuffers.clear();

    for (uint32_t i = 0; i < m_vRegisteredResources.size(); ++i)
    {
        if (m_vRegisteredResources[i])
        {
            nvenc_fns.nvEncUnregisterResource(encoder_ptr, m_vRegisteredResources[i]);
        }
    }
    m_vRegisteredResources.clear();


    for (uint32_t i = 0; i < m_vRegisteredResourcesForReference.size(); ++i)
    {
        if (m_vRegisteredResourcesForReference[i])
        {
            nvenc_fns.nvEncUnregisterResource(encoder_ptr, m_vRegisteredResourcesForReference[i]);
        }
    }
    m_vRegisteredResourcesForReference.clear();

}


void NvEncoderNew::WaitForCompletionEvent(int iEvent)
{
#if defined(_WIN32)
    // Check if we are in async mode. If not, don't wait for event;
    NV_ENC_CONFIG sEncodeConfig = { 0 };
    NV_ENC_INITIALIZE_PARAMS sInitializeParams = { 0 };
    sInitializeParams.encodeConfig = &sEncodeConfig;
    GetInitializeParams(&sInitializeParams);

    if (0U == sInitializeParams.enableEncodeAsync)
    {
        return;
    }
#ifdef DEBUG
    WaitForSingleObject(m_vpCompletionEvent[iEvent], INFINITE);
#else
    // wait for 20s which is infinite on terms of gpu time
    if (WaitForSingleObject(m_vpCompletionEvent[iEvent], 20000) == WAIT_FAILED)
    {
        LOG_CRITICAL("Failed to encode frame", NV_ENC_ERR_GENERIC);
    }
#endif
#endif
}

uint32_t NvEncoderNew::GetWidthInBytes(const NV_ENC_BUFFER_FORMAT bufferFormat, const uint32_t width)
{
    switch (bufferFormat) {
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
        LOG_CRITICAL("Invalid Buffer format", NV_ENC_ERR_INVALID_PARAM);
        return 0;
    }
}

uint32_t NvEncoderNew::GetNumChromaPlanes(const NV_ENC_BUFFER_FORMAT bufferFormat)
{
    switch (bufferFormat) 
    {
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
        LOG_CRITICAL("Invalid Buffer format", NV_ENC_ERR_INVALID_PARAM);
        return -1;
    }
}

uint32_t NvEncoderNew::GetChromaPitch(const NV_ENC_BUFFER_FORMAT bufferFormat,const uint32_t lumaPitch)
{
    switch (bufferFormat)
    {
    case NV_ENC_BUFFER_FORMAT_NV12:
    case NV_ENC_BUFFER_FORMAT_YUV420_10BIT:
    case NV_ENC_BUFFER_FORMAT_YUV444:
    case NV_ENC_BUFFER_FORMAT_YUV444_10BIT:
        return lumaPitch;
    case NV_ENC_BUFFER_FORMAT_YV12:
    case NV_ENC_BUFFER_FORMAT_IYUV:
        return (lumaPitch + 1)/2;
    case NV_ENC_BUFFER_FORMAT_ARGB:
    case NV_ENC_BUFFER_FORMAT_ARGB10:
    case NV_ENC_BUFFER_FORMAT_AYUV:
    case NV_ENC_BUFFER_FORMAT_ABGR:
    case NV_ENC_BUFFER_FORMAT_ABGR10:
        return 0;
    default:
        LOG_CRITICAL("Invalid Buffer format", NV_ENC_ERR_INVALID_PARAM);
        return -1;
    }
}

void NvEncoderNew::GetChromaSubPlaneOffsets(const NV_ENC_BUFFER_FORMAT bufferFormat, const uint32_t pitch, const uint32_t height, std::vector<uint32_t>& chromaOffsets)
{
    chromaOffsets.clear();
    switch (bufferFormat)
    {
    case NV_ENC_BUFFER_FORMAT_NV12:
    case NV_ENC_BUFFER_FORMAT_YUV420_10BIT:
        chromaOffsets.push_back(pitch * height);
        return;
    case NV_ENC_BUFFER_FORMAT_YV12:
    case NV_ENC_BUFFER_FORMAT_IYUV:
        chromaOffsets.push_back(pitch * height);
        chromaOffsets.push_back(chromaOffsets[0] + (NvEncoderNew::GetChromaPitch(bufferFormat, pitch) * GetChromaHeight(bufferFormat, height)));
        return;
    case NV_ENC_BUFFER_FORMAT_YUV444:
    case NV_ENC_BUFFER_FORMAT_YUV444_10BIT:
        chromaOffsets.push_back(pitch * height);
        chromaOffsets.push_back(chromaOffsets[0] + (pitch * height));
        return;
    case NV_ENC_BUFFER_FORMAT_ARGB:
    case NV_ENC_BUFFER_FORMAT_ARGB10:
    case NV_ENC_BUFFER_FORMAT_AYUV:
    case NV_ENC_BUFFER_FORMAT_ABGR:
    case NV_ENC_BUFFER_FORMAT_ABGR10:
        return;
    default:
        LOG_CRITICAL("Invalid Buffer format", NV_ENC_ERR_INVALID_PARAM);
        return;
    }
}

uint32_t NvEncoderNew::GetChromaHeight(const NV_ENC_BUFFER_FORMAT bufferFormat, const uint32_t lumaHeight)
{
    switch (bufferFormat)
    {
    case NV_ENC_BUFFER_FORMAT_YV12:
    case NV_ENC_BUFFER_FORMAT_IYUV:
    case NV_ENC_BUFFER_FORMAT_NV12:
    case NV_ENC_BUFFER_FORMAT_YUV420_10BIT:
        return (lumaHeight + 1)/2;
    case NV_ENC_BUFFER_FORMAT_YUV444:
    case NV_ENC_BUFFER_FORMAT_YUV444_10BIT:
        return lumaHeight;
    case NV_ENC_BUFFER_FORMAT_ARGB:
    case NV_ENC_BUFFER_FORMAT_ARGB10:
    case NV_ENC_BUFFER_FORMAT_AYUV:
    case NV_ENC_BUFFER_FORMAT_ABGR:
    case NV_ENC_BUFFER_FORMAT_ABGR10:
        return 0;
    default:
        LOG_CRITICAL("Invalid Buffer format", NV_ENC_ERR_INVALID_PARAM);
        return 0;
    }
}

uint32_t NvEncoderNew::GetChromaWidthInBytes(const NV_ENC_BUFFER_FORMAT bufferFormat, const uint32_t lumaWidth)
{
    switch (bufferFormat)
    {
    case NV_ENC_BUFFER_FORMAT_YV12:
    case NV_ENC_BUFFER_FORMAT_IYUV:
        return (lumaWidth + 1) / 2;
    case NV_ENC_BUFFER_FORMAT_NV12:
        return lumaWidth;
    case NV_ENC_BUFFER_FORMAT_YUV420_10BIT:
        return 2 * lumaWidth;
    case NV_ENC_BUFFER_FORMAT_YUV444:
        return lumaWidth;
    case NV_ENC_BUFFER_FORMAT_YUV444_10BIT:
        return 2 * lumaWidth;
    case NV_ENC_BUFFER_FORMAT_ARGB:
    case NV_ENC_BUFFER_FORMAT_ARGB10:
    case NV_ENC_BUFFER_FORMAT_AYUV:
    case NV_ENC_BUFFER_FORMAT_ABGR:
    case NV_ENC_BUFFER_FORMAT_ABGR10:
        return 0;
    default:
        LOG_CRITICAL("Invalid Buffer format", NV_ENC_ERR_INVALID_PARAM);
        return 0;
    }
}


int NvEncoderNew::GetCapabilityValue(GUID guidCodec, NV_ENC_CAPS capsToQuery)
{
    if (!encoder_ptr)
    {
        return 0;
    }
    NV_ENC_CAPS_PARAM capsParam = { NV_ENC_CAPS_PARAM_VER };
    capsParam.capsToQuery = capsToQuery;
    int v;
    nvenc_fns.nvEncGetEncodeCaps(encoder_ptr, guidCodec, &capsParam, &v);
    return v;
}

int NvEncoderNew::GetFrameSize() const
{
    switch (GetPixelFormat())
    {
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
        LOG_CRITICAL("Invalid Buffer format", NV_ENC_ERR_INVALID_PARAM);
        return 0;
    }
}

void NvEncoderNew::GetInitializeParams(NV_ENC_INITIALIZE_PARAMS *pInitializeParams)
{
    if (!pInitializeParams || !pInitializeParams->encodeConfig)
    {
        LOG_CRITICAL("Both pInitializeParams and pInitializeParams->encodeConfig can't be NULL", NV_ENC_ERR_INVALID_PTR);
    }
    NV_ENC_CONFIG *pEncodeConfig = pInitializeParams->encodeConfig;
    *pEncodeConfig = encode_cfg;
    *pInitializeParams = init_params;
    pInitializeParams->encodeConfig = pEncodeConfig;
}

void NvEncoderNew::InitializeBitstreamBuffer()
{
    for (int i = 0; i < m_nEncoderBuffer; i++)
    {
        NV_ENC_CREATE_BITSTREAM_BUFFER createBitstreamBuffer = { NV_ENC_CREATE_BITSTREAM_BUFFER_VER };
        (nvenc_fns.nvEncCreateBitstreamBuffer(encoder_ptr, &createBitstreamBuffer));
        m_vBitstreamOutputBuffer[i] = createBitstreamBuffer.bitstreamBuffer;
    }
}

void NvEncoderNew::DestroyBitstreamBuffer()
{
    for (uint32_t i = 0; i < m_vBitstreamOutputBuffer.size(); i++)
    {
        if (m_vBitstreamOutputBuffer[i])
        {
            nvenc_fns.nvEncDestroyBitstreamBuffer(encoder_ptr, m_vBitstreamOutputBuffer[i]);
        }
    }

    m_vBitstreamOutputBuffer.clear();
}

void NvEncoderNew::InitializeMVOutputBuffer()
{
    for (int i = 0; i < m_nEncoderBuffer; i++)
    {
        NV_ENC_CREATE_MV_BUFFER createMVBuffer = { NV_ENC_CREATE_MV_BUFFER_VER };
        (nvenc_fns.nvEncCreateMVBuffer(encoder_ptr, &createMVBuffer));
        m_vMVDataOutputBuffer.push_back(createMVBuffer.mvBuffer);
    }
}

void NvEncoderNew::DestroyMVOutputBuffer()
{
    for (uint32_t i = 0; i < m_vMVDataOutputBuffer.size(); i++)
    {
        if (m_vMVDataOutputBuffer[i])
        {
            nvenc_fns.nvEncDestroyMVBuffer(encoder_ptr, m_vMVDataOutputBuffer[i]);
        }
    }

    m_vMVDataOutputBuffer.clear();
}

NVENCSTATUS NvEncoderNew::DoMotionEstimation(NV_ENC_INPUT_PTR inputBuffer, NV_ENC_INPUT_PTR inputBufferForReference, NV_ENC_OUTPUT_PTR outputBuffer)
{
    NV_ENC_MEONLY_PARAMS meParams = { NV_ENC_MEONLY_PARAMS_VER };
    meParams.inputBuffer = inputBuffer;
    meParams.referenceFrame = inputBufferForReference;
    meParams.inputWidth = GetEncodeWidth();
    meParams.inputHeight = GetEncodeHeight();
    meParams.mvBuffer = outputBuffer;
    meParams.completionEvent = GetCompletionEvent(m_iToSend % m_nEncoderBuffer);
    NVENCSTATUS nvStatus = nvenc_fns.nvEncRunMotionEstimationOnly(encoder_ptr, &meParams);
    
    return nvStatus;
}
