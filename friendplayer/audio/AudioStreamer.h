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
	inline static constexpr int ENCODED_SAMPLE_RATE = 48000;
	inline static constexpr int ENCODED_CHANNEL_COUNT = 2;
	inline static constexpr int OPUS_FRAME_SIZE = ENCODED_SAMPLE_RATE / 100;
	inline static constexpr int OPUS_MAX_FRAME_SIZE = OPUS_FRAME_SIZE;

	// sizeof(s16) * stereo
	inline static constexpr int BYTES_PER_FRAME = 2 * ENCODED_CHANNEL_COUNT;
	inline static constexpr int BYTES_PER_OPUS_FRAME = OPUS_FRAME_SIZE * BYTES_PER_FRAME;
	inline static constexpr int SAMPLES_PER_OPUS_FRAME = BYTES_PER_OPUS_FRAME / 2;

	AudioStreamer();

	bool InitRender();
	bool InitEncoder(uint32_t bitrate);
	bool InitDecoder();

	bool WaitForCapture(HANDLE* signals);
	bool CaptureAudio(std::vector<uint8_t>& raw_out);

	void PlayAudio(const std::vector<uint8_t>& raw_in);

	bool EncodeAudio(const std::vector<uint8_t>& raw_in, std::vector<uint8_t>& enc_out);
	bool DecodeAudio(const std::vector<uint8_t>& enc_in, std::vector<uint8_t>& raw_out);

	
private:


	IMMDevice* device_;
	IAudioClient* client_;
	IAudioCaptureClient* capture_;
	// Feed in empty sound during no capture
	IAudioRenderClient* render_;
	HANDLE receive_signal_;
    HANDLE stop_signal_;

	OpusEncoder* encoder;
	OpusDecoder* decoder;
	WAVEFORMATEX* system_format;

	SwrContext* context;
	
	int resample_size;
	
	std::vector<opus_int16> decode_output_buffer;
	uint8_t* resample_buffer;
};

