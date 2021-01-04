#pragma once

#include <Windows.h>
#include <Audioclient.h>
#include <mmdeviceapi.h>
#include <stdint.h>
#include <opus/opus.h>
#include <vector>
extern "C" {
#include <libavutil/channel_layout.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
}

class AudioStreamer {
public:
	AudioStreamer(uint16_t max_packet_size);

	bool InitEncoder(uint32_t bitrate);
	bool InitDecoder(uint32_t sample_rate, uint32_t num_channels);

	bool BeginEncode(const uint8_t* data, uint32_t size);
	bool EncodeAudio(uint8_t* out, uint32_t* packet_size);
	void EndEncode();

	bool DecodeAudio(uint8_t* data, uint32_t size, uint8_t** out, uint32_t* out_size_samples);
	void EndDecode();

private:

	IMMDevice* device_;
	IAudioClient* client_;
	IAudioCaptureClient* capture_;
	// Feed in empty sound during no capture
	IAudioRenderClient* render_;

	OpusEncoder* encoder;
	OpusDecoder* decoder;
	WAVEFORMATEX* stream_format;

	SwrContext* context;
	std::vector<opus_int16> codec_buffer;
	std::vector<uint8_t> output_buffer;
	uint32_t output_offset;
	
	uint8_t* resample_buffer;
	uint32_t resample_size;

	uint16_t frame_size;
	uint16_t max_frame_size;
	uint16_t max_packet_size;

	opus_int32 decode_sample_rate;
	opus_int32 decode_num_channels;
};

