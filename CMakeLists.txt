cmake_minimum_required(VERSION 3.15)
project(VirtualDubPipeFilter LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

set(SDK_ROOT "${CMAKE_SOURCE_DIR}/../VDPluginSDK-1.2")

add_library(VDXFrame STATIC
    "${SDK_ROOT}/src/VDXFrame/source/stdafx.cpp"
    "${SDK_ROOT}/src/VDXFrame/source/VideoFilter.cpp"
    "${SDK_ROOT}/src/VDXFrame/source/VideoFilterEntry.cpp"
    "${SDK_ROOT}/src/VDXFrame/source/VideoFilterDialog.cpp"
)
target_include_directories(VDXFrame PUBLIC
    "${SDK_ROOT}/include"
    "${SDK_ROOT}/src/VDXFrame/include"
)
target_compile_definitions(VDXFrame PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_SECURE_NO_DEPRECATE)

add_library(PipeFilter SHARED
    src/main.cpp
    src/PipeFilter.cpp
    src/PipeFilter.rc
)
target_link_libraries(PipeFilter PRIVATE VDXFrame)
target_include_directories(PipeFilter PRIVATE
    "${SDK_ROOT}/include"
    "${SDK_ROOT}/src/VDXFrame/include"
    src
)
target_compile_definitions(PipeFilter PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_SECURE_NO_DEPRECATE)
set_target_properties(PipeFilter PROPERTIES
    SUFFIX ".vdf"
    PREFIX ""
)
