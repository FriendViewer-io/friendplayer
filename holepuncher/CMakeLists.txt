add_library(puncher-proto-objects OBJECT "${CMAKE_CURRENT_LIST_DIR}/puncher_messages.proto")

set(PROTOBUF_IMPORT_DIRS
  "${CMAKE_CURRENT_LIST_DIR}"
  "${PROJECT_SOURCE_DIR}/externals/protobuf/src")

target_link_libraries(puncher-proto-objects PUBLIC protobuf::libprotobuf)
protobuf_generate(
  TARGET puncher-proto-objects
  OUT_VAR PROTO_GEN_FILES
  LANGUAGE cpp
  IMPORT_DIRS ${PROTOBUF_IMPORT_DIRS}
  PROTOC_OPTIONS "--experimental_allow_proto3_optional")

target_include_directories(puncher-proto-objects PUBLIC "${CMAKE_BINARY_DIR}/holepuncher")