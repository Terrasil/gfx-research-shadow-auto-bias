cmake_minimum_required(VERSION 3.25)

project(gfx-research-shadow-auto-bias VERSION 0.1.4 LANGUAGES C CXX)

option(GFX_RESEARCH_BUILD_APP "Build the interactive OpenGL experiment" ON)
option(GFX_RESEARCH_STATIC_RUNTIME "Link the Windows compiler runtime statically" ON)
set(GFX_RESEARCH_BASE_GIT_TAG v0.3 CACHE STRING "gfx-research-base Git revision")
set(GFX_RESEARCH_BASE_LOCAL_PATH "" CACHE PATH "Optional local gfx-research-base checkout")

if(MSVC AND GFX_RESEARCH_STATIC_RUNTIME)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

find_package(Threads REQUIRED)

add_library(shadow-bias-benchmark STATIC src/benchmark.cpp src/run_paths.cpp)
target_compile_features(shadow-bias-benchmark PUBLIC cxx_std_20)
target_compile_definitions(shadow-bias-benchmark PUBLIC GFX_RESEARCH_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(shadow-bias-benchmark PUBLIC Threads::Threads)

function(shadow_bias_strict_float target)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /fp:precise)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -ffp-contract=off)
    endif()
endfunction()
shadow_bias_strict_float(shadow-bias-benchmark)

add_executable(shadow-auto-bias-benchmark src/benchmark_main.cpp)
target_link_libraries(shadow-auto-bias-benchmark PRIVATE shadow-bias-benchmark)
shadow_bias_strict_float(shadow-auto-bias-benchmark)

if(GFX_RESEARCH_BUILD_APP)
    if(GFX_RESEARCH_BASE_LOCAL_PATH)
        set(GFX_RESEARCH_BASE_BUILD_EXAMPLE OFF CACHE BOOL "Build the base example" FORCE)
        add_subdirectory("${GFX_RESEARCH_BASE_LOCAL_PATH}" "${CMAKE_BINARY_DIR}/gfx-research-base")
    else()
        if(NOT DEFINED CPM_SOURCE_CACHE)
            set(CPM_SOURCE_CACHE "${CMAKE_CURRENT_SOURCE_DIR}/.cpm-cache" CACHE PATH "CPM source cache")
        endif()
        include(cmake/CPM.cmake)
        set(GFX_RESEARCH_THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty" CACHE PATH "Third-party source directory")
        CPMAddPackage(
            NAME gfx_research_base
            GITHUB_REPOSITORY Terrasil/gfx-research-base
            GIT_TAG "${GFX_RESEARCH_BASE_GIT_TAG}"
            SOURCE_DIR "${GFX_RESEARCH_THIRD_PARTY_DIR}/gfx-research-base"
            OPTIONS "GFX_RESEARCH_BASE_BUILD_EXAMPLE OFF"
        )
    endif()

    # Share the context's entry points with the ImGui backend, including OSMesa.
    if(TARGET gfx_research_imgui AND TARGET gfx_research_glad)
        target_link_libraries(gfx_research_imgui PRIVATE gfx_research_glad)
        target_compile_definitions(gfx_research_imgui PRIVATE IMGUI_IMPL_OPENGL_LOADER_CUSTOM)
        if(MSVC)
            target_compile_options(gfx_research_imgui PRIVATE /FIglad/gl.h)
        else()
            target_compile_options(gfx_research_imgui PRIVATE -include glad/gl.h)
        endif()
    endif()

    add_library(shadow-bias-desktop OBJECT
        src/main.cpp
        src/live_renderer.cpp
        src/publication_figure.cpp
        src/publication_suite.cpp
        src/reference_viewer.cpp
        src/shadow_pass.cpp
        src/shadow_renderer.cpp
    )
    target_link_libraries(shadow-bias-desktop PUBLIC shadow-bias-benchmark gfx::research_base)
    target_include_directories(shadow-bias-desktop SYSTEM PRIVATE "${GFX_RESEARCH_BASE_THIRD_PARTY_DIR}/tinygltf")
    shadow_bias_strict_float(shadow-bias-desktop)

    add_executable(gfx-research-shadow-auto-bias)
    add_executable(shadow-auto-bias)
    add_custom_target(shadow-bias-assets
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
            "$<TARGET_FILE_DIR:gfx-research-shadow-auto-bias>/shaders"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/heightmaps"
            "$<TARGET_FILE_DIR:gfx-research-shadow-auto-bias>/heightmaps"
        VERBATIM
    )

    foreach(app IN ITEMS gfx-research-shadow-auto-bias shadow-auto-bias)
        target_link_libraries(${app} PRIVATE shadow-bias-desktop)
        add_dependencies(${app} shadow-bias-assets)
    endforeach()
    if(COMMAND gfx_research_configure_executable)
        gfx_research_configure_executable(gfx-research-shadow-auto-bias)
        gfx_research_configure_executable(shadow-auto-bias)
        gfx_research_configure_executable(shadow-auto-bias-benchmark)
    endif()
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(shadow-auto-bias-reference-tests tests/reference_math.cpp)
    target_compile_features(shadow-auto-bias-reference-tests PRIVATE cxx_std_20)
    target_link_libraries(shadow-auto-bias-reference-tests PRIVATE Threads::Threads)
    shadow_bias_strict_float(shadow-auto-bias-reference-tests)
    add_test(NAME shadow-auto-bias-reference COMMAND shadow-auto-bias-reference-tests)
endif()
