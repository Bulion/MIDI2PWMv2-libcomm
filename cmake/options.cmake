option(LIBCOMM_BUILD_EXAMPLES "Build libcomm example applications" ON)
option(LIBCOMM_BUILD_TESTS "Build libcomm unit tests" OFF)

if(MSVC)
    add_compile_options(/permissive-)
endif()
