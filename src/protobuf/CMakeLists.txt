add_library(cpp-proto-objects OBJECT
  "${CMAKE_CURRENT_LIST_DIR}/client_messages.proto"
  "${CMAKE_CURRENT_LIST_DIR}/network_messages.proto"
  "${CMAKE_CURRENT_LIST_DIR}/actor_messages.proto"
  "${CMAKE_CURRENT_LIST_DIR}/host_messages.proto")

set(CPP_PROTOBUF_IMPORT_DIRS
  "${CMAKE_CURRENT_LIST_DIR}"
  "${PROJECT_SOURCE_DIR}/externals/protobuf/src")

target_link_libraries(cpp-proto-objects PUBLIC protobuf::libprotobuf)
protobuf_generate(
  TARGET cpp-proto-objects
  OUT_VAR PROTO_GEN_FILES
  LANGUAGE cpp
  IMPORT_DIRS ${CPP_PROTOBUF_IMPORT_DIRS}
  PROTOC_OPTIONS "--experimental_allow_proto3_optional")

target_include_directories(cpp-proto-objects PUBLIC "${CMAKE_BINARY_DIR}/src")