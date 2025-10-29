if(NOT LIBCOMM_BUILD_TESTS)
    return()
endif()

include(CTest)

if(NOT BUILD_TESTING)
    message(STATUS "libcomm tests disabled because BUILD_TESTING=OFF")
    return()
endif()

add_subdirectory(tests)
