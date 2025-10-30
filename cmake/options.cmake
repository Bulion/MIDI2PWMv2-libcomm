option(LIBCOMM_BUILD_EXAMPLES "Build libcomm example applications" ON)
option(LIBCOMM_BUILD_TESTS "Build libcomm unit tests" OFF)

set(LIBCOMM_LOG_LEVEL "LOG_LEVEL_INFO" CACHE STRING "Maximum log level to compile into libcomm (LOG_LEVEL_OFF, LOG_LEVEL_ERROR, LOG_LEVEL_WARN, LOG_LEVEL_INFO, LOG_LEVEL_DEBUG)")
set_property(CACHE LIBCOMM_LOG_LEVEL PROPERTY STRINGS "LOG_LEVEL_OFF" "LOG_LEVEL_ERROR" "LOG_LEVEL_WARN" "LOG_LEVEL_INFO" "LOG_LEVEL_DEBUG")

if(MSVC)
    add_compile_options(/permissive-)
endif()
