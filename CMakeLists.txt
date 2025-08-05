cmake_minimum_required(VERSION 3.20)

project(libcomm VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(libcomm src/libcomm.cpp)

target_include_directories(libcomm
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/includes>
)

add_subdirectory(dependencies)

