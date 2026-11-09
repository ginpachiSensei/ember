add_executable(ember_cooker main.cpp)
target_link_libraries(ember_cooker PRIVATE ember::core ember::asset)
ember_configure_target(ember_cooker "tools")

# Cooks content/ into <build>/cooked on every build. The cooker is incremental: unchanged sources
# (same content hash) are skipped, so this is quick after the first run.
set(EMBER_COOKED_DIR "${CMAKE_BINARY_DIR}/cooked" CACHE INTERNAL "")
add_custom_target(ember_cook ALL
    COMMAND ember_cooker --content "${PROJECT_SOURCE_DIR}/content" --out "${EMBER_COOKED_DIR}"
    COMMENT "Cooking assets"
    VERBATIM
)
add_dependencies(ember_cook ember_cooker)
set_target_properties(ember_cook PROPERTIES FOLDER "tools")
