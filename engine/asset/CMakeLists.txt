find_package(Stb REQUIRED)
find_path(CGLTF_INCLUDE_DIRS "cgltf.h" REQUIRED)

add_library(ember_asset STATIC
    src/GltfLoader.cpp
    src/Procedural.cpp
    src/StbImpl.cpp
)
add_library(ember::asset ALIAS ember_asset)

target_include_directories(ember_asset PUBLIC include)
target_include_directories(ember_asset SYSTEM PRIVATE ${Stb_INCLUDE_DIR} ${CGLTF_INCLUDE_DIRS})
target_link_libraries(ember_asset PUBLIC ember::core)
ember_configure_target(ember_asset "engine")
