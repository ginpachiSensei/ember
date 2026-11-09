# Compiles every top-level shaders/*.slang to SPIR-V in <build>/shaders/<name>.spv.
# Files in shaders/core/ are shared includes, not compiled on their own.
# The renderer recompiles changed files at runtime too (hot reload), using the same slangc and flags.

find_package(slang CONFIG REQUIRED) # provides SLANGC_EXECUTABLE

set(EMBER_SLANGC "${SLANGC_EXECUTABLE}" CACHE INTERNAL "")
# The one place these flags live: the runtime hot reloader gets the same list (see engine/render).
set(EMBER_SLANG_FLAGS
    -target spirv
    -profile spirv_1_6
    -fvk-use-entrypoint-name   # keep vsMain/psMain names instead of renaming every entry to "main"
    -fvk-use-scalar-layout     # buffers use C-like layout, so C++ structs match without padding
    -matrix-layout-column-major
    -default-image-format-unknown # storage images take their format from the image, not a guess (rgba32f)
    -warnings-disable 39001,41012 # bindless arrays intentionally alias bindings
    -O2
    CACHE INTERNAL "")
set(EMBER_SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/shaders" CACHE INTERNAL "")

file(GLOB EMBER_SHADER_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.slang")
file(GLOB EMBER_SHADER_INCLUDES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/core/*.slang")

set(outputs)
foreach(source ${EMBER_SHADER_SOURCES})
    get_filename_component(name "${source}" NAME_WE)
    set(output "${EMBER_SHADER_OUTPUT_DIR}/${name}.spv")
    add_custom_command(
        OUTPUT "${output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${EMBER_SHADER_OUTPUT_DIR}"
        COMMAND "${SLANGC_EXECUTABLE}" "${source}" ${EMBER_SLANG_FLAGS} -I "${CMAKE_CURRENT_SOURCE_DIR}" -o "${output}"
        DEPENDS "${source}" ${EMBER_SHADER_INCLUDES}
        COMMENT "slangc ${name}.slang"
        VERBATIM
    )
    list(APPEND outputs "${output}")
endforeach()

add_custom_target(ember_shaders ALL DEPENDS ${outputs} SOURCES ${EMBER_SHADER_SOURCES} ${EMBER_SHADER_INCLUDES})
set_target_properties(ember_shaders PROPERTIES FOLDER "engine")
