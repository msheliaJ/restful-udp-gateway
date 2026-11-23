cmake_minimum_required(VERSION 3.16)
project(RESTfulCloudService CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

# Packet format shared with the Windows client
set(PROTOCOL_DIR ${CMAKE_SOURCE_DIR}/../common)

add_executable(Project main.cpp)
target_include_directories(Project PRIVATE ${CMAKE_SOURCE_DIR} ${PROTOCOL_DIR})
target_link_libraries(Project PRIVATE Threads::Threads)

# ---------------------------------------------------------------------------
# Unit tests and coverage (used by CI; OFF by default so the Docker image
# build is unchanged)
# ---------------------------------------------------------------------------
option(GATEWAY_BUILD_TESTS "Build the GoogleTest unit tests" OFF)
option(GATEWAY_COVERAGE "Instrument the tests for gcov coverage" OFF)

if(GATEWAY_BUILD_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)

    add_executable(gateway_tests tests/test_protocol.cpp tests/test_routes.cpp)
    target_include_directories(gateway_tests PRIVATE ${CMAKE_SOURCE_DIR} ${PROTOCOL_DIR})
    target_link_libraries(gateway_tests PRIVATE GTest::gtest_main Threads::Threads)

    if(GATEWAY_COVERAGE)
        target_compile_options(gateway_tests PRIVATE --coverage -O0 -g)
        target_link_options(gateway_tests PRIVATE --coverage)
    endif()

    include(GoogleTest)
    # Run from server/ so the "/" route can find public/index.html
    gtest_discover_tests(gateway_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()
