#include "AudioStreamer.h"

#include "common/Log.h"

namespace {
inline AVSampleFormat GetSampleFormat(const WAVEFORMATEX* wave_format)
{
    switch (wave_format->wFormatTag) {
    case WAVE_FORMAT_PCM:
        if (16 == wave_format->wBitsPerSample) {
            return AV_SAMPLE_FMT_S16;
        }
        if (32 == wave_format->wBitsPerSample) {
            return AV_SAMPLE_FMT_S32;
        }
        break;
    case WAVE_FORMAT_IEEE_FLOAT:
        return AV_SAMPLE_FMT_FLT;
    case WAVE_FORMAT_ALAW:
    case WAVE_FORMAT_MULAW:
        return AV_SAMPLE_FMT_U8;
    case WAVE_FORMAT_EXTENSIBLE:
    {
        const WAVEFORMATEXTENSIBLE* wfe = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(wave_format);
        if (KSDATAFORMAT_SUBTYPE_IEEE_FLOAT == wfe->SubFormat) {
            return AV_SAMPLE_FMT_FLT;
        }
        if (KSDATAFORMAT_SUBTYPE_PCM == wfe->SubFormat) {
            if (16 == wave_format->wBitsPerSample) {
                return AV_SAMPLE_FMT_S16;
            }
            if (32 == wave_format->wBitsPerSample) {
                return AV_SAMPLE_FMT_S32;
            }
        }
        break;
    }
    default:
        break;
    }
    return AV_SAMPLE_FMT_NONE;
}
}

AudioStreamer::AudioStreamer() {
    
}

bool AudioStreamer::InitRender() {
    LPBYTE buffer;
    uint32_t frames;
    IAudioClient* client_tmp;
    WAVEFORMATEX* wfex;
    if (FAILED(device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
        reinterpret_cast<void**>(&client_tmp)))) {
        return false;
    }

    if (FAILED(client_tmp->GetMixFormat(&wfex))) {
        return false;
    }

    if (FAILED(client_tmp->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 20000, 0, wfex, nullptr))) {
        return false;
    }

    if (FAILED(client_tmp->GetBufferSize(&frames))) {
        return false;
    }

    if (FAILED(client_tmp->GetService(__uuidof(IAudioRenderClient),
        reinterpret_cast<void**>(&render_)))) {
        return false;
    }

    if (FAILED(render_->GetBuffer(frames, &buffer))) {
        return false;
    }

    memset(buffer, 0, frames * wfex->nBlockAlign);
    if (FAILED(render_->ReleaseBuffer(frames, 0))) {
        return false;
    }

    return SUCCEEDED(client_tmp->Start());
}

bool AudioStreamer::InitEncoder(uint32_t bitrate) {
    int err;

    if (FAILED(CoInitialize(nullptr))) {
        return false;
    }

    IMMDeviceEnumerator* enumerator = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        reinterpret_cast<void**>(&enumerator)))) {
        return false;
    }

    if (FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device_))) {
        return false;
    }

    if (FAILED(device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
        reinterpret_cast<void**>(&client_)))) {
        return false;
    }

    if (FAILED(client_->GetMixFormat(&system_format))) {
        return false;
    }
    
    receive_signal_ = CreateEvent(nullptr, false, false, nullptr);
    stop_signal_ = CreateEvent(nullptr, false, false, nullptr);

    if (FAILED(client_->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_LOOPBACK, 20000, 0, system_format, nullptr))) {
        return false;
    }
    uint32_t frames;

    if (FAILED(client_->GetBufferSize(&frames))) {
        return false;
    }

    if (!InitRender()) {
        return false;
    }

    if (FAILED(client_->GetService(__uuidof(IAudioCaptureClient),
        reinterpret_cast<void**>(&capture_)))) {
        return false;
    }
    if (FAILED(client_->SetEventHandle(receive_signal_))) {
        return false;
    }

    client_->Start();

	encoder = opus_encoder_create(ENCODED_SAMPLE_RATE, ENCODED_CHANNEL_COUNT, OPUS_APPLICATION_RESTRICTED_LOWDELAY, &err);
    if (err < 0) {
        LOG_ERROR("Failed to create opus encoder: {}", err);
        return false;
    }

    err = opus_encoder_ctl(encoder, OPUS_SET_BITRATE(bitrate));
    if (err < 0) {
        LOG_ERROR("Failed to set opus encoder bitrate: {}", err);
        return false;
    }

    context = swr_alloc_set_opts(
        nullptr, av_get_default_channel_layout(ENCODED_CHANNEL_COUNT), AV_SAMPLE_FMT_S16, ENCODED_SAMPLE_RATE,
        av_get_default_channel_layout(system_format->nChannels), GetSampleFormat(system_format), system_format->nSamplesPerSec,
        0, nullptr);
    swr_init(context);

    resample_size = static_cast<int>(av_rescale_rnd(swr_get_delay(context, system_format->nSamplesPerSec) + OPUS_FRAME_SIZE,
        ENCODED_SAMPLE_RATE, system_format->nSamplesPerSec, AV_ROUND_UP));
    av_samples_alloc(&resample_buffer, nullptr, ENCODED_CHANNEL_COUNT, resample_size, AV_SAMPLE_FMT_S16, 0);

    return true;
}

bool AudioStreamer::WaitForCapture(HANDLE* signals) {
    auto ret = WaitForMultipleObjects(2, signals, false, 10);
    if (!(ret == WAIT_OBJECT_0 || ret == WAIT_TIMEOUT)) {
        LOG_WARNING("Unknown error HRESULT={}", ret);
    }
    return ret == WAIT_OBJECT_0 || ret == WAIT_TIMEOUT;
}

bool AudioStreamer::CaptureAudio(std::string& raw_out) {
    HANDLE signals[] = { receive_signal_, stop_signal_ };
    static std::string wrapover_buf;

    raw_out = std::move(wrapover_buf);
    wrapover_buf.clear();

    const int requested_bytes = OPUS_FRAME_SIZE * system_format->nBlockAlign;

    while(raw_out.size() < requested_bytes) {
        WaitForCapture(signals);

        uint32_t capture_size = 0;
        LPBYTE buffer;
        uint32_t frames;
        DWORD flags;
        uint64_t pos, ts;

        while (true) {
            auto ret = capture_->GetNextPacketSize(&capture_size);
            if (!capture_size) {
                break;
            }

            capture_->GetBuffer(&buffer, &frames, &flags, &pos, &ts);
            raw_out.insert(raw_out.end(), buffer, buffer + (frames * system_format->nBlockAlign));
            capture_->ReleaseBuffer(frames);
        }
    }

    if (raw_out.size() > requested_bytes) {
        wrapover_buf.resize(raw_out.size() - requested_bytes);
        std::copy(raw_out.begin() + requested_bytes, raw_out.end(), wrapover_buf.begin());
    }
    return true;
}

bool AudioStreamer::InitDecoder() {
    int err;

    if (FAILED(CoInitialize(nullptr))) {
        return false;
    }

    IMMDeviceEnumerator* enumerator = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        reinterpret_cast<void**>(&enumerator)))) {
        return false;
    }

    if (FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device_))) {
        return false;
    }

    if (FAILED(device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
        reinterpret_cast<void**>(&client_)))) {
        return false;
    }

    if (FAILED(client_->GetMixFormat(&system_format))) {
        return false;
    }

    if (FAILED(client_->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 1000000, 0, system_format, nullptr))) {
        return false;
    }

    if (FAILED(client_->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(&render_)))) {
        return false;
    }
    
    client_->Start();

    decoder = opus_decoder_create(ENCODED_SAMPLE_RATE, ENCODED_CHANNEL_COUNT, &err);
    if (err < 0) {
        LOG_ERROR("Failed to create opus decoder: {}", err);
        return false;
    }

    context = swr_alloc_set_opts(
        nullptr, av_get_default_channel_layout(system_format->nChannels), GetSampleFormat(system_format), system_format->nSamplesPerSec,
        av_get_default_channel_layout(ENCODED_CHANNEL_COUNT), AV_SAMPLE_FMT_S16, ENCODED_SAMPLE_RATE,
        0, nullptr);
    swr_init(context);

    resample_size = static_cast<int>(av_rescale_rnd(swr_get_delay(context, ENCODED_SAMPLE_RATE) + OPUS_FRAME_SIZE,
        system_format->nSamplesPerSec, ENCODED_SAMPLE_RATE, AV_ROUND_UP));

    return true;
}

bool AudioStreamer::EncodeAudio(const std::string& raw_in, std::string& enc_out) {
    opus_int32 encoded_size = 0;
    const uint8_t* cvt_in[] = { reinterpret_cast<const uint8_t*>(raw_in.data()) };
    swr_convert(context, &resample_buffer, OPUS_FRAME_SIZE, cvt_in, OPUS_FRAME_SIZE);
    
    int max_output_per_frame = 1275 * 3;
    if (enc_out.size() != max_output_per_frame) {
        enc_out.resize(max_output_per_frame);
    }

    encoded_size = opus_encode(encoder, reinterpret_cast<opus_int16*>(resample_buffer), OPUS_FRAME_SIZE,
        reinterpret_cast<unsigned char*>(enc_out.data()), max_output_per_frame);
    if (encoded_size < 0) {
        LOG_ERROR("Encode failed: {}", opus_strerror(encoded_size));
        return false;
    }
    enc_out.resize(encoded_size);

    return true;
}

bool AudioStreamer::DecodeAudio(const std::string& enc_in, std::string& raw_out) {
    if (decode_output_buffer.size() != SAMPLES_PER_OPUS_FRAME) {
        decode_output_buffer.resize(SAMPLES_PER_OPUS_FRAME);
    }

    int num_samples = opus_decode(decoder, reinterpret_cast<const unsigned char*>(enc_in.data()), static_cast<opus_int32>(enc_in.size()),
        decode_output_buffer.data(), static_cast<int>(decode_output_buffer.size()), 0);
    //LOG_INFO("Decoder: Num samples decoded = {}", num_samples);
    if (num_samples < 0) {
        LOG_ERROR("Decode failed: {}", opus_strerror(num_samples));
        return false;
    }
    int system_frame_size = OPUS_FRAME_SIZE * system_format->nChannels * (system_format->wBitsPerSample / 8);
    raw_out.resize(system_frame_size);

    uint8_t* raw_out_in[] = { reinterpret_cast<uint8_t*>(raw_out.data()) };
    const uint8_t* opus_out_in[] = {reinterpret_cast<uint8_t*>(decode_output_buffer.data())};
    swr_convert(context, raw_out_in, OPUS_FRAME_SIZE, opus_out_in, OPUS_FRAME_SIZE);

    LOG_TRACE("num samples: {}", num_samples);

    return true;
}

void AudioStreamer::PlayAudio(const std::string& raw_out) {
    UINT pad_amt, buf_sz, avail_sz;
    
    client_->GetCurrentPadding(&pad_amt);
    client_->GetBufferSize(&buf_sz);

    avail_sz = buf_sz - pad_amt;
    UINT write_sz = static_cast<UINT>(raw_out.size() / system_format->nBlockAlign);

    LOG_TRACE("Current buffer size and padding size: {} {}", buf_sz, pad_amt);

    BYTE* buf = nullptr;
    while (buf == nullptr) {
        auto result = render_->GetBuffer(write_sz, &buf);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    std::copy(raw_out.begin(), raw_out.begin() +
        (write_sz * system_format->nBlockAlign), buf);

    render_->ReleaseBuffer(write_sz, 0);
}