cmake_minimum_required(VERSION 3.20)
project(imgui)

set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/imgui)
set(CMAKE_DEBUG_POSTFIX d)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED)

add_library(imgui STATIC
	${IMGUI_DIR}/imgui.cpp
	${IMGUI_DIR}/imgui.h
	${IMGUI_DIR}/imstb_rectpack.h
	${IMGUI_DIR}/imstb_textedit.h
	${IMGUI_DIR}/imstb_truetype.h
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_internal.h
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
)
target_include_directories(imgui PUBLIC
    $<BUILD_INTERFACE:${IMGUI_DIR}>
    $<INSTALL_INTERFACE:include>)
	
set(INSTALL_TARGETS imgui)
set(INSTALL_HEADERS ${IMGUI_DIR}/imgui.h ${IMGUI_DIR}/imconfig.h)

foreach(BACKEND glfw)
	set(NAME imgui_impl_${BACKEND})
	set(HEADER ${IMGUI_DIR}/backends/${NAME}.h)
	add_library(${NAME} STATIC ${IMGUI_DIR}/backends/${NAME}.cpp ${HEADER})
	target_link_libraries(${NAME} PUBLIC imgui)
	target_include_directories(${NAME} PUBLIC
	    $<BUILD_INTERFACE:${IMGUI_DIR}/backends>
	    $<INSTALL_INTERFACE:include>)
	LIST(APPEND INSTALL_TARGETS ${NAME})
	LIST(APPEND INSTALL_HEADERS ${HEADER})
endforeach()

target_link_libraries(imgui_impl_glfw PRIVATE glfw)

