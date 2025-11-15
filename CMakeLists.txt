cmake_minimum_required(VERSION 3.28...4.1)

project(friendplayer)
enable_language(CUDA)

# Needed for ASIO sadly...
include(cmake/CPM.cmake)

# All external libraries
include(cmake/asio-load.cmake)
add_subdirectory(externals/CLI11)
set(GLFW_LIBRARY_TYPE STATIC)
add_subdirectory(externals/glfw)
add_subdirectory(externals/imgui)
add_subdirectory(externals/cryptopp-cmake)
add_subdirectory(externals/opus)
add_subdirectory(externals/spdlog)
add_subdirectory(externals/nvenc)
add_subdirectory(externals/protobuf)

# To get protobuf generation and compilation working in cmake
include(externals/protobuf/cmake/protobuf-generate.cmake)

# Gather all srcs and generate executable
add_subdirectory(src)
