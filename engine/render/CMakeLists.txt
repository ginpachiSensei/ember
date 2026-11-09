add_library(ember_render STATIC
    src/Renderer.cpp
    src/ShaderLibrary.cpp
    src/ShaderLibrary.h
    src/GpuTypes.h
)
add_library(ember::render ALIAS ember_render)

target_include_directories(ember_render PUBLIC include)
target_link_libraries(ember_render PUBLIC ember::core ember::rhi ember::asset)
add_dependencies(ember_render ember_shaders)

# Dev-time paths for loading shaders and hot-reloading them with the same compiler and flags as the build.
list(JOIN EMBER_SLANG_FLAGS " " slang_flags)
target_compile_definitions(ember_render PRIVATE
    EMBER_SHADER_BINARY_DIR="${EMBER_SHADER_OUTPUT_DIR}"
    EMBER_SHADER_SOURCE_DIR="${PROJECT_SOURCE_DIR}/shaders"
    EMBER_SLANGC_PATH="${EMBER_SLANGC}"
    EMBER_SLANG_FLAGS="${slang_flags}"
)
ember_configure_target(ember_render "engine")
