find_package(imgui CONFIG REQUIRED)

add_library(ember_debugui STATIC
    src/Theme.cpp
)
add_library(ember::debugui ALIAS ember_debugui)

target_include_directories(ember_debugui PUBLIC include)
# Font Awesome icon codepoints (ICON_FA_* macros) for use in UI labels.
target_include_directories(ember_debugui SYSTEM PUBLIC "${PROJECT_SOURCE_DIR}/external/icons")
target_link_libraries(ember_debugui PUBLIC ember::core imgui::imgui)
ember_configure_target(ember_debugui "engine")
