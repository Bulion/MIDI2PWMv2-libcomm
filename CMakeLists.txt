cmake_minimum_required(VERSION 3.20)

project(libcomm VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(cmake/options.cmake)
include(cmake/dependencies.cmake)
include(cmake/library.cmake)
include(cmake/examples.cmake)
