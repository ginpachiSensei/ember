add_executable(ember_sandbox
    src/main.cpp
    src/Editor.cpp
    src/Sandbox.h
)
target_link_libraries(ember_sandbox PRIVATE
    ember::core ember::platform ember::rhi ember::scene ember::asset ember::render ember::debugui)
add_dependencies(ember_sandbox ember_cook) # assets are cooked before the sandbox runs

# Development paths: source content, cooked output, and the cooker (for asset hot reload).
target_compile_definitions(ember_sandbox PRIVATE
    EMBER_CONTENT_DIR="${PROJECT_SOURCE_DIR}/content"
    EMBER_COOKED_DIR="${EMBER_COOKED_DIR}"
    EMBER_COOKER_EXE="$<TARGET_FILE:ember_cooker>"
)
ember_configure_target(ember_sandbox "apps")
