#include "AudioStreamer.h"

#include "common/Log.h"

AudioStreamer::AudioStreamer() {
    
}

bool AudioStreamer::InitRender() {
    return false;}

bool AudioStreamer::InitEncoder(uint32_t bitrate) {
    return false;
}

bool AudioStreamer::WaitForCapture(HANDLE* signals) {
    return false;
}

bool AudioStreamer::CaptureAudio(std::string& raw_out) {
    return false;
}

bool AudioStreamer::InitDecoder() {
    return false;
}

bool AudioStreamer::EncodeAudio(const std::string& raw_in, std::string& enc_out) {
    return false;
}

bool AudioStreamer::DecodeAudio(const std::string& enc_in, std::string& raw_out) {
    return false;
}

void AudioStreamer::PlayAudio(const std::string& raw_out) {
    return;
}

void AudioStreamer::SetVolume(double volume) {
    return;
}