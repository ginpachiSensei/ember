find_package(SDL3 CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)

add_library(ember_platform STATIC
    src/Window.cpp
)
add_library(ember::platform ALIAS ember_platform)

target_include_directories(ember_platform PUBLIC include)
target_link_libraries(ember_platform
    PUBLIC ember::core imgui::imgui
    PRIVATE SDL3::SDL3 # SDL stays an implementation detail of this module
)
ember_configure_target(ember_platform "engine")
