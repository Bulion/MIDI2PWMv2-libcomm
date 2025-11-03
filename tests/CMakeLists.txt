include(FetchContent)

FetchContent_Declare(
    catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.2
)
FetchContent_MakeAvailable(catch2)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)

find_package(Threads REQUIRED)

add_executable(libcomm_tests
    frame_transport_tests.cpp
    midi_endpoint_tests.cpp
    pwm_endpoint_tests.cpp
    stream_processor_tests.cpp
)
target_compile_features(libcomm_tests PRIVATE cxx_std_20)

target_link_libraries(libcomm_tests
    PRIVATE
        libcomm
        Catch2::Catch2WithMain
        Threads::Threads
)

catch_discover_tests(libcomm_tests)
