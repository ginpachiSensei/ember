add_executable(ember_sandbox
    src/main.cpp
)
target_link_libraries(ember_sandbox PRIVATE ember::core ember::platform ember::rhi)
ember_configure_target(ember_sandbox "apps")
