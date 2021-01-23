#include "actors/AudioDecodeActor.h"

#include "actors/CommonActorNames.h"
#include "common/Log.h"
#include "protobuf/network_messages.pb.h"
#include "streamer/AudioStreamer.h"

AudioDecodeActor::AudioDecodeActor(const ActorMap& actor_map, DataBufferMap& buffer_map, std::string&& name)
    : Actor(actor_map, buffer_map, std::move(name)), stream_num(-1) {
    audio_streamer = std::make_unique<AudioStreamer>();
}

void AudioDecodeActor::OnInit(const std::optional<any_msg>& init_msg) {
    audio_streamer = std::make_unique<AudioStreamer>();
    audio_streamer->InitDecoder();
    if (init_msg) {
        fp_actor::AudioDecodeInit decode_init_msg;
        init_msg->UnpackTo(&decode_init_msg);
        stream_num = decode_init_msg.stream_num();
    }
}

void AudioDecodeActor::OnMessage(const any_msg& msg) {
    if (msg.Is<fp_actor::AudioData>()) {
        fp_actor::AudioData audio_data;
        msg.UnpackTo(&audio_data);
        OnAudioFrame(audio_data);
    } else {
        Actor::OnMessage(msg);
    }
}

void AudioDecodeActor::OnAudioFrame(const fp_actor::AudioData& audio_data) {
    if (audio_data.stream_num() != stream_num) {
        LOG_ERROR("Data frame sent to wrong stream, received on {} but expected {}", stream_num, audio_data.stream_num());
        return;
    }
    std::string* data = buffer_map.GetBuffer(audio_data.handle());
    std::string* out_data = new std::string();
    audio_streamer->DecodeAudio(*data, *out_data);
    buffer_map.Decrement(audio_data.handle());
    audio_streamer->PlayAudio(*out_data);
    delete out_data;
}

// void audio_thread_client(std::shared_ptr<ClientSocket> sock) {
//     AudioStreamer audio_streamer;
//    
//     audio_streamer.InitDecoder();
//    
//     std::vector<uint8_t> enc_frame_out, raw_frame_out;
//     enc_frame_out.resize(20 * 1024);
//
//     while (protocol_mgr->HasClients()) {
//         auto frame_start = std::chrono::system_clock::now();
//         auto last_now = frame_start;
//         RetrievedBuffer enc_frame_wrapper(enc_frame_out.data(), enc_frame_out.size());
//         sock->GetAudioFrame(enc_frame_wrapper);
//         auto get_elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - last_now);
//         last_now = std::chrono::system_clock::now();
//         std::vector<uint8_t> enc_frame(enc_frame_wrapper.data_out.begin(), enc_frame_wrapper.data_out.end());
//
//         if (enc_frame.empty()) { continue; }
//
//         audio_streamer.DecodeAudio(enc_frame, raw_frame_out);
//         auto decode_elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - last_now);
//         last_now = std::chrono::system_clock::now();
//         if (raw_frame_out.size() > 0) {
//             audio_streamer.PlayAudio(raw_frame_out);
//         }
//         auto play_elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - last_now);
//         LOG_TRACE("Elapsed times: {} {} {}", get_elapsed.count(), decode_elapsed.count(), play_elapsed.count());
//     }
// }