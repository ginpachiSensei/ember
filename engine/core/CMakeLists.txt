add_library(ember_core STATIC
    src/Guid.cpp
    src/Jobs.cpp
    src/Log.cpp
    src/Memory.cpp
    src/Serialize.cpp
)
add_library(ember::core ALIAS ember_core)

target_include_directories(ember_core PUBLIC include)
find_package(glm CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
target_link_libraries(ember_core PUBLIC glm::glm nlohmann_json::nlohmann_json)
ember_configure_target(ember_core "engine")

if(EMBER_ENABLE_PROFILING)
    find_package(Tracy CONFIG REQUIRED)
    target_link_libraries(ember_core PUBLIC Tracy::TracyClient)
    target_compile_definitions(ember_core PUBLIC EMBER_PROFILING=1)
endif()

if(EMBER_BUILD_TESTS)
    add_executable(ember_core_tests
        tests/TestMain.cpp
        tests/SlotMapTests.cpp
        tests/MemoryTests.cpp
        tests/JobsTests.cpp
        tests/ReflectTests.cpp
    )
    target_link_libraries(ember_core_tests PRIVATE ember::core doctest::doctest)
    ember_configure_target(ember_core_tests "tests")
    add_test(NAME ember_core_tests COMMAND ember_core_tests)
endif()
