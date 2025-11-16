include_directories("${CMAKE_CURRENT_LIST_DIR}")

add_subdirectory(protobuf)

add_executable(friendplayer friendviewer_main.cpp)

# Actor sources
target_sources(friendplayer
PRIVATE
  actors/Actor.cpp
  actors/Actor.h
  actors/ActorEnvironment.cpp
  actors/ActorEnvironment.h
  actors/ActorGenerator.cpp
  actors/ActorGenerator.h
  actors/ActorMap.cpp
  actors/ActorMap.h
  actors/ActorType.h
  actors/AdminActor.cpp
  actors/AdminActor.h
  actors/AudioDecodeActor.cpp
  actors/AudioDecodeActor.h
  actors/AudioEncodeActor.cpp
  actors/AudioEncodeActor.h
  actors/BaseActor.cpp
  actors/BaseActor.h
  actors/ClientActor.cpp
  actors/ClientActor.h
  actors/ClientManagerActor.cpp
  actors/ClientManagerActor.h
  actors/CommonActorNames.h
  actors/DataBuffer.cpp
  actors/DataBuffer.h
  actors/HeartbeatActor.cpp
  actors/HeartbeatActor.h
  actors/HostActor.cpp
  actors/HostActor.h
  actors/HostSettingsActor.cpp
  actors/HostSettingsActor.h
  actors/InputActor.cpp
  actors/InputActor.h
  actors/ProtocolActor.cpp
  actors/ProtocolActor.h
  actors/SocketActor.cpp
  actors/SocketActor.h
  actors/TimerActor.cpp
  actors/TimerActor.h
  actors/VideoDecodeActor.cpp
  actors/VideoDecodeActor.h
  actors/VideoEncodeActor.cpp
  actors/VideoEncodeActor.h)

# Common sources
target_sources(friendplayer
PRIVATE
  common/ColorSpace.cu
  common/ColorSpace.h
  common/Config.cpp
  common/Config.h
  common/Crypto.cpp
  common/Crypto.h
  common/FrameRingBuffer.cpp
  common/FrameRingBuffer.h
  common/Log.cpp
  common/Log.h
  common/NvCodecUtils.h
  common/Timer.cpp
  common/Timer.h)
set_source_files_properties(ColorSpace.cu PROPERTIES LANGUAGE CUDA)

# Decoder sources
target_sources(friendplayer
PRIVATE
  decoder/FramePresenterGL.cpp
  decoder/FramePresenterGL.h
  decoder/NvDecoder.cpp
  decoder/NvDecoder.h)

# Encoder sources
target_sources(friendplayer
PRIVATE
  encoder/DDAImpl.cpp
  encoder/DDAImpl.h
  encoder/NvEncoder.cpp
  encoder/NvEncoder.h)

# Streamer sources
target_sources(friendplayer
PRIVATE
  streamer/AudioStreamer.cpp
  streamer/AudioStreamer.h
  streamer/InputStreamer.cpp
  streamer/InputStreamer.h
  streamer/VideoStreamer.cpp
  streamer/VideoStreamer.h)

if (TARGET imgui_impl_glfw)
  message("glfw backend present\n\n\n\n")
endif()

# TODO: ViGEm
target_link_libraries(friendplayer
PUBLIC
  asio
  CLI11
  concurrentqueue
  cpp-proto-objects
  cryptopp
  fmt
  libglew_static
  glfw
  imgui
  imgui_impl_glfw
  imgui_impl_opengl3
  libprotobuf
  nvcuvid
  nvencodeapi
  opus
  puncher-proto-objects
  spdlog)

if (WIN32)
  target_link_libraries(friendplayer PUBLIC d3d11.lib dxgi.lib winmm.lib cuda.lib XInput.lib XInput9_1_0.lib Setupapi.lib)
  target_link_libraries(friendplayer PUBLIC ViGEmClient)
endif()
