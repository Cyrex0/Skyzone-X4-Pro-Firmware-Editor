cmake_minimum_required(VERSION 3.20)
project(SkyZoneEditor VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Fetch dependencies ──────────────────────────────────────────────
include(FetchContent)

FetchContent_Declare(imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.91.8.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_Declare(SDL2
    URL https://github.com/libsdl-org/SDL/releases/download/release-2.30.11/SDL2-2.30.11.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(SDL2)
FetchContent_MakeAvailable(imgui)

# ── ImGui library ───────────────────────────────────────────────────
set(IMGUI_DIR ${imgui_SOURCE_DIR})
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)
target_link_libraries(imgui PUBLIC SDL2::SDL2)
if(WIN32)
    target_link_libraries(imgui PUBLIC opengl32)
else()
    find_package(OpenGL REQUIRED)
    target_link_libraries(imgui PUBLIC OpenGL::GL)
endif()

# ── Main application ───────────────────────────────────────────────
add_executable(SkyZoneEditor WIN32
    src/main.cpp
    src/firmware.cpp
    src/firmware_a.cpp
    src/disasm.cpp
    src/annotations.cpp
    src/decompile.cpp
    src/ui_editor.cpp
    src/ui_disasm.cpp
)
target_include_directories(SkyZoneEditor PRIVATE src)
target_link_libraries(SkyZoneEditor PRIVATE imgui SDL2::SDL2main)
if(WIN32)
    target_link_libraries(SkyZoneEditor PRIVATE SDL2::SDL2)
    target_compile_definitions(SkyZoneEditor PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
endif()

# Copy SDL2.dll next to exe on Windows
if(WIN32)
    add_custom_command(TARGET SkyZoneEditor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:SkyZoneEditor>
    )
endif()
