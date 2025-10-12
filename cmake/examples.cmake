if(NOT LIBCOMM_BUILD_EXAMPLES)
    return()
endif()

find_package(PkgConfig QUIET)
if(NOT PkgConfig_FOUND)
    message(WARNING "PkgConfig not found, skipping D-Bus example applications")
    return()
endif()

pkg_check_modules(DBUS QUIET dbus-1 IMPORTED_TARGET GLOBAL)
if(NOT DBUS_FOUND)
    message(WARNING "dbus-1 development package not found, skipping D-Bus example applications")
    return()
endif()

add_library(libcomm_dbus_transport STATIC
    examples/dbus_transport/dbus_transport.cpp
)
target_include_directories(libcomm_dbus_transport
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/dbus_transport
)
target_link_libraries(libcomm_dbus_transport
    PUBLIC
        libcomm
        PkgConfig::DBUS
)
target_compile_features(libcomm_dbus_transport PUBLIC cxx_std_17)
if(MSVC)
    target_compile_options(libcomm_dbus_transport PRIVATE /W4)
else()
    target_compile_options(libcomm_dbus_transport PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(libcomm_dbus_receiver
    examples/dbus_receiver/main.cpp
)
target_link_libraries(libcomm_dbus_receiver PRIVATE libcomm_dbus_transport)

add_executable(libcomm_dbus_sender
    examples/dbus_sender/main.cpp
)
target_link_libraries(libcomm_dbus_sender PRIVATE libcomm_dbus_transport)
