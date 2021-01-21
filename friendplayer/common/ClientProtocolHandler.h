// #pragma once

// #include "common/ProtocolHandler.h"
// #include "common/FrameRingBuffer.h"

// #include "protobuf/common_messages.pb.h"
// #include "protobuf/client_messages.pb.h"
// #include "protobuf/host_messages.pb.h"

// class ClientSocket;

// // Client management from host perspective
// class ClientProtocolHandler : public ProtocolHandler {
// private:
//     static constexpr size_t VIDEO_FRAME_BUFFER = 5;
//     static constexpr size_t AUDIO_FRAME_BUFFER = 5;
//     // Guess values, tune or scale these?
//     static constexpr size_t VIDEO_FRAME_SIZE = 20000;
//     static constexpr size_t AUDIO_FRAME_SIZE = 1795;

// public:
//     ClientProtocolHandler(asio_endpoint endpoint);

//     uint32_t GetVideoFrame(RetrievedBuffer& buf_in);
//     uint32_t GetAudioFrame(RetrievedBuffer& buf_in);
    
//     virtual ~ClientProtocolHandler() {}
    
// private:

//     FrameRingBuffer video_buffer;
//     FrameRingBuffer audio_buffer;


//     // add enc/dec here -- adam! :)

//     // GUARDED BY recv_message_queue_m
//     virtual bool DoHandshake();
//     // GUARDED BY recv_message_queue_m
//     virtual void OnDataFrame(const fp_network::Data& msg);
//     // GUARDED BY recv_message_queue_m
//     virtual void OnStateMessage(const fp_proto::StateMessage& msg);    

//     void OnVideoFrame(const fp_proto::HostDataFrame& msg);
//     void OnAudioFrame(const fp_proto::HostDataFrame& msg);
//     void OnHostState(const fp_proto::HostState& msg);
// };