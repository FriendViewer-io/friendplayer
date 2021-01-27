1 time setup:

    vcpkg install imgui[glfw-binding,opengl3-glew-binding]:x64-windows-static
    vcpkg install cli11:x64-windows-static
    vcpkg install asio:x64-windows-static
    vcpkg install cryptopp:x64-windows-static
    vcpkg install opus:x64-windows-static
    vcpkg install spdlog:x64-windows-static
    vcpkg install protobuf:x64-windows-static

    vcpkg integrate install

    Copy $(VCPKG_ROOT)\installed\x64-windows-static\include\google to $(VCPKG_ROOT)\installed\x64-windows-static\tools\protobuf


Generate protobuf files

    protoc *.proto --cpp_out=. --experimental_allow_proto3_optional