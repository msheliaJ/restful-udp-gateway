cmake_minimum_required(VERSION 3.16)
project(RESTfulCloudService CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

add_executable(Project main.cpp)
target_include_directories(Project PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(Project PRIVATE Threads::Threads)
