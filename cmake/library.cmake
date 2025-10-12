set(LIBCOMM_SCHEMA "${CMAKE_CURRENT_SOURCE_DIR}/schemas/comm.fbs")
set(LIBCOMM_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")

add_custom_command(
    OUTPUT "${LIBCOMM_GENERATED_DIR}/comm_generated.h"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${LIBCOMM_GENERATED_DIR}"
    COMMAND "${LIBCOMM_FLATC_COMMAND}" --cpp --scoped-enums -o "${LIBCOMM_GENERATED_DIR}" "${LIBCOMM_SCHEMA}"
    DEPENDS "${LIBCOMM_SCHEMA}"
    VERBATIM
    COMMENT "Generating FlatBuffers sources from ${LIBCOMM_SCHEMA}"
)

add_custom_target(libcomm_generate ALL
    DEPENDS "${LIBCOMM_GENERATED_DIR}/comm_generated.h"
)

add_library(libcomm STATIC
    src/endpoint.cpp
)
add_dependencies(libcomm libcomm_generate)

target_include_directories(libcomm
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${LIBCOMM_GENERATED_DIR}>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(libcomm PUBLIC cxx_std_17)

target_link_libraries(libcomm
    PUBLIC
        ${LIBCOMM_FLATBUFFERS_TARGET}
        ${LIBCOMM_ETL_TARGET}
)

if(MSVC)
    target_compile_options(libcomm PRIVATE /W4)
else()
    target_compile_options(libcomm PRIVATE -Wall -Wextra -Wpedantic)
endif()
