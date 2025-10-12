option(LIBCOMM_BUILD_EXAMPLES "Build libcomm example applications" ON)

if(MSVC)
    add_compile_options(/permissive-)
endif()
