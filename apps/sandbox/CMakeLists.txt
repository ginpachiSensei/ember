add_executable(ember_sandbox
    src/main.cpp
)
target_link_libraries(ember_sandbox PRIVATE ember::core ember::platform ember::rhi ember::debugui)
# Temporary: load content straight from the source tree until the asset system exists (phase 2).
target_compile_definitions(ember_sandbox PRIVATE EMBER_CONTENT_DIR="${PROJECT_SOURCE_DIR}/content")
ember_configure_target(ember_sandbox "apps")
