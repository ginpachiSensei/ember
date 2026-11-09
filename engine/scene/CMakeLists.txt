find_package(EnTT CONFIG REQUIRED)

add_library(ember_scene STATIC
    src/Components.cpp
    src/Scene.cpp
)
add_library(ember::scene ALIAS ember_scene)

target_include_directories(ember_scene PUBLIC include)
target_link_libraries(ember_scene PUBLIC ember::core EnTT::EnTT)
ember_configure_target(ember_scene "engine")

if(EMBER_BUILD_TESTS)
    add_executable(ember_scene_tests ../core/tests/TestMain.cpp tests/SceneTests.cpp)
    target_link_libraries(ember_scene_tests PRIVATE ember::scene doctest::doctest)
    ember_configure_target(ember_scene_tests "tests")
    add_test(NAME ember_scene_tests COMMAND ember_scene_tests)
endif()
