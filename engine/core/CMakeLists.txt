add_library(ember_core STATIC
    src/Log.cpp
)
add_library(ember::core ALIAS ember_core)

target_include_directories(ember_core PUBLIC include)
ember_configure_target(ember_core "engine")

if(EMBER_ENABLE_PROFILING)
    find_package(Tracy CONFIG REQUIRED)
    target_link_libraries(ember_core PUBLIC Tracy::TracyClient)
    target_compile_definitions(ember_core PUBLIC EMBER_PROFILING=1)
endif()

if(EMBER_BUILD_TESTS)
    find_package(doctest CONFIG REQUIRED)

    add_executable(ember_core_tests
        tests/TestMain.cpp
        tests/SlotMapTests.cpp
    )
    target_link_libraries(ember_core_tests PRIVATE ember::core doctest::doctest)
    ember_configure_target(ember_core_tests "tests")
    add_test(NAME ember_core_tests COMMAND ember_core_tests)
endif()
