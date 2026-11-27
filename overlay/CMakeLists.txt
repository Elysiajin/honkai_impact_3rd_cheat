cmake_minimum_required(VERSION 3.20)
project(bh3_overlay CXX ASM)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()

set(ROOT        "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(IMGUI_DIR   "${ROOT}/libs/ImGuiSaphire")
set(CXXMCP_DIR  "D:/My Projects/cxxmcp-master")
set(CXXMCP_LIB  "D:/My Projects/cxxmcp-build-mingw")
set(ZYDIS_DIR   "D:/My Projects/zydis-master")
set(ZYDIS_LIB   "D:/zydis-build-mingw")

set(SRC
    ov_gui.cpp
    ov_mcp.cpp
    ov_x64call.S
    ui/UiCore.cpp
    ui/Widgets.cpp
    ui/State.cpp
    ui/Pages.cpp
    ui/PageActivity.cpp
    ui/PageLua.cpp
    ui/App.cpp
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_dx11.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_win32.cpp"
)

add_library(bh3_overlay SHARED ${SRC})

target_include_directories(bh3_overlay PRIVATE
    "${IMGUI_DIR}"
    "${IMGUI_DIR}/backends"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CXXMCP_DIR}/sdk/include"
    "${CXXMCP_DIR}/sdk/server/include"
    "${CXXMCP_DIR}/sdk/client/include"
    "${CXXMCP_DIR}/sdk/protocol/include"
    "${CXXMCP_DIR}/sdk/transport/include"
    "${CXXMCP_DIR}/sdk/core/include"
    "${CXXMCP_DIR}/third_party"
    "${ZYDIS_DIR}/include"
    "${ZYDIS_DIR}/dependencies/zycore-c-1.5.1/include"
)

target_compile_definitions(bh3_overlay PRIVATE CXXMCP_ENABLE_HTTP=1)
target_compile_options(bh3_overlay PRIVATE
    -O2 -Wall -Wno-unused-parameter -Wno-missing-field-initializers
)

target_link_directories(bh3_overlay PRIVATE
    "${CXXMCP_LIB}"
    "${ZYDIS_LIB}"
    "${ZYDIS_LIB}/zycore"
)

target_link_libraries(bh3_overlay PRIVATE
    mcp_server mcp_client mcp_protocol
    Zydis Zycore
    pthread ws2_32 crypt32 dwmapi ole32 uuid gdi32 user32 d3d11 dxgi version
    -static -static-libgcc -static-libstdc++
)

set_target_properties(bh3_overlay PROPERTIES
    PREFIX ""
    OUTPUT_NAME "bh3_overlay"
)
