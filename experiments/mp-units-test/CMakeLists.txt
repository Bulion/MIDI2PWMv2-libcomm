cmake_minimum_required(VERSION 3.20)
project(mp_units_eval LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
    mp-units
    GIT_REPOSITORY https://github.com/mpusz/mp-units.git
    GIT_TAG        v2.2.0
    SOURCE_SUBDIR  src
)
FetchContent_MakeAvailable(mp-units)

add_executable(without_mp_units test_without_mp_units.cpp)
target_compile_options(without_mp_units PRIVATE -Os)

add_executable(with_mp_units test_with_mp_units.cpp)
target_link_libraries(with_mp_units PRIVATE mp-units::mp-units)
target_compile_options(with_mp_units PRIVATE -Os)
