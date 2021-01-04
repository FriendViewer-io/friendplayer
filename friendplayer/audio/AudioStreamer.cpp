#include "AudioStreamer.h"

#include "common/Log.h"

AudioStreamer::AudioStreamer(uint16_t max_packet_size) : max_packet_size(max_packet_size) {
    
}


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

    if (FAILED(client_->GetMixFormat(&stream_format))) {
        return false;
    }

	encoder = opus_encoder_create(stream_format->nSamplesPerSec, stream_format->nChannels, OPUS_APPLICATION_AUDIO, &err);
    if (err < 0) {
        LOG_ERROR("Failed to create opus encoder: {}", err);
        return false;
    }

    err = opus_encoder_ctl(encoder, OPUS_SET_BITRATE(bitrate));
    if (err < 0) {
        LOG_ERROR("Failed to set opus encoder bitrate: {}", err);
        return false;
    }

    frame_size = static_cast<uint16_t>(stream_format->nSamplesPerSec / 100);
    max_frame_size = frame_size * 3;

    codec_buffer.resize(frame_size * stream_format->nChannels);

    context = swr_alloc_set_opts(
        nullptr, av_get_default_channel_layout(stream_format->nChannels), AV_SAMPLE_FMT_S16, stream_format->nSamplesPerSec,
        av_get_default_channel_layout(stream_format->nChannels), GetSampleFormat(stream_format), stream_format->nSamplesPerSec,
        0, 0);
    swr_init(context);

    return true;
}

bool AudioStreamer::InitDecoder(uint32_t sample_rate, uint32_t num_channels) {
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

    if (FAILED(client_->GetMixFormat(&stream_format))) {
        return false;
    }

    decode_sample_rate = sample_rate;
    decode_num_channels = num_channels;
    decoder = opus_decoder_create(sample_rate, num_channels, &err);
    if (err < 0) {
        LOG_ERROR("Failed to create opus decoder: {}", err);
        return false;
    }

    frame_size = static_cast<uint16_t>(sample_rate / 100);
    max_frame_size = frame_size * 3;

    codec_buffer.resize(frame_size * num_channels);
    output_buffer.resize(max_packet_size);

    context = swr_alloc_set_opts(
        nullptr, av_get_default_channel_layout(stream_format->nChannels), GetSampleFormat(stream_format), stream_format->nSamplesPerSec,
        av_get_default_channel_layout(num_channels), AV_SAMPLE_FMT_S16, sample_rate,
        0, 0);
    swr_init(context);
}

bool AudioStreamer::BeginEncode(const uint8_t* data, uint32_t size) {
    output_offset = 0;
    
    uint32_t num_input_samples = size / (stream_format->nChannels * stream_format->wBitsPerSample / 8);
    int num_output_samples;
    num_output_samples = av_rescale_rnd(swr_get_delay(context, stream_format->nSamplesPerSec) + num_input_samples,
        stream_format->nSamplesPerSec, stream_format->nSamplesPerSec, AV_ROUND_UP);
    av_samples_alloc(&resample_buffer, NULL, stream_format->nChannels, num_output_samples, AV_SAMPLE_FMT_FLT, 0);
    resample_size = swr_convert(context, &resample_buffer, num_output_samples, &data, num_input_samples);
    LOG_INFO("Encoder: Number of samples = {}", resample_size);
    resample_size *= stream_format->nChannels * stream_format->wBitsPerSample / 8;
    LOG_INFO("Encoder: Total resample size = {}", resample_size);

    return true;
}

bool AudioStreamer::EncodeAudio(uint8_t* out, uint32_t* packet_size) {
    opus_int32 encoded_size = 0;
    *packet_size = 0;
    *out = 0;
    if (output_offset >= resample_size) {
        return true;
    }
    for (int i = 0; i < stream_format->nChannels * frame_size; ++i) {
        codec_buffer[i] = resample_buffer[i * 2 + 1 + output_offset] << 8 | resample_buffer[i * 2 + output_offset];
    }

    encoded_size = opus_encode(encoder, codec_buffer.data(), frame_size, out, max_packet_size);
    if (encoded_size < 0) {
        LOG_ERROR("Encode failed: {}", opus_strerror(encoded_size));
        return false;
    }
    *packet_size = encoded_size;
    output_offset += stream_format->nChannels * frame_size;

    return true;
}

void AudioStreamer::EndEncode() {
    //LOG_INFO("Total size of output = {}", offset);
    av_freep(&resample_buffer);
}

bool AudioStreamer::DecodeAudio(uint8_t* data, uint32_t size, uint8_t** out, uint32_t* out_size_samples) {

    uint32_t num_input_samples = size / (decode_num_channels * 2);
    resample_size = av_rescale_rnd(swr_get_delay(context, decode_sample_rate) + num_input_samples,
        stream_format->nSamplesPerSec, decode_sample_rate, AV_ROUND_UP);
    av_samples_alloc(&resample_buffer, NULL, 2, resample_size, AV_SAMPLE_FMT_S16, 0);

    int num_samples = opus_decode(decoder, data, size, codec_buffer.data(), max_frame_size, 0);
    LOG_INFO("Decoder: Num samples decoded = {}", num_samples);
    if (num_samples < 0) {
        LOG_ERROR("Decode failed: {}", opus_strerror(num_samples));
        //av_freep(&resample_buffer);
        return false;
    }

    for (int i = 0; i < decode_num_channels * num_samples; i++) {
        output_buffer[2 * i] = codec_buffer[i] & 0xFF;
        output_buffer[2 * i + 1] = (codec_buffer[i] >> 8) & 0xFF;
    }
    
    const uint8_t* in_bufs[] = { output_buffer.data() };
    *out_size_samples = swr_convert(context, &resample_buffer, resample_size, in_bufs, num_samples);
    *out = resample_buffer;
    LOG_INFO("Decoder: Number of samples out = {}", *out_size_samples);
    //*out_size *= stream_format->nChannels * stream_format->wBitsPerSample / 8;
    //LOG_INFO("Total size = {}", *size);

    return true;
}


void AudioStreamer::EndDecode() {
    av_freep(&resample_buffer);
    //LOG_INFO("Total size of output = {}", offset);
}
