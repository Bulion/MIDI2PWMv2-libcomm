include(FetchContent)

set(FLATBUFFERS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_BUILD_FLATC ON CACHE BOOL "" FORCE)
set(FLATBUFFERS_BUILD_FLATHASH OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_BUILD_GRPCTEST OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_BUILD_SHAREDLIB OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_INSTALL OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_CPP_STD 17 CACHE STRING "" FORCE)

FetchContent_Declare(
    flatbuffers
    GIT_REPOSITORY https://github.com/google/flatbuffers.git
    GIT_TAG        v2.0.8
)
FetchContent_MakeAvailable(flatbuffers)

set(LIBCOMM_FLATBUFFERS_TARGET "")
if(TARGET flatbuffers::flatbuffers)
    set(LIBCOMM_FLATBUFFERS_TARGET flatbuffers::flatbuffers)
    target_compile_definitions(flatbuffers::flatbuffers PRIVATE FLATBUFFERS_NO_ABSOLUTE_PATH_RESOLUTION)
elseif(TARGET flatbuffers)
    set(LIBCOMM_FLATBUFFERS_TARGET flatbuffers)
    target_compile_definitions(flatbuffers PRIVATE FLATBUFFERS_NO_ABSOLUTE_PATH_RESOLUTION)
else()
    message(FATAL_ERROR "flatbuffers target not available after FetchContent")
endif()

if(TARGET flatbuffers::flatc)
    set(LIBCOMM_FLATC_COMMAND $<TARGET_FILE:flatbuffers::flatc>)
else()
    find_program(LIBCOMM_FLATC_COMMAND flatc)
    if(NOT LIBCOMM_FLATC_COMMAND)
        message(FATAL_ERROR "flatc compiler not found. Install flatbuffers or enable building flatc.")
    endif()
endif()

FetchContent_Declare(
    etl
    GIT_REPOSITORY https://github.com/ETLCPP/etl.git
    GIT_TAG        20.37.1
)
FetchContent_MakeAvailable(etl)

set(LIBCOMM_ETL_TARGET "")
if(TARGET etl::etl)
    set(LIBCOMM_ETL_TARGET etl::etl)
    get_target_property(LIBCOMM_ETL_BASE_TARGET etl::etl ALIASED_TARGET)
    if(NOT LIBCOMM_ETL_BASE_TARGET)
        set(LIBCOMM_ETL_BASE_TARGET etl::etl)
    endif()
elseif(TARGET etl)
    set(LIBCOMM_ETL_TARGET etl)
    set(LIBCOMM_ETL_BASE_TARGET etl)
else()
    message(FATAL_ERROR "etl target not available after FetchContent")
endif()

if(LIBCOMM_ETL_BASE_TARGET)
    target_compile_definitions(${LIBCOMM_ETL_BASE_TARGET}
        INTERFACE
            ETL_NO_STL
            ETL_TARGET_OS_CMSIS_OS2
    )
endif()
