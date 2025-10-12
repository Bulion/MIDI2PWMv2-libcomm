set(LIBCOMM_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(LIBCOMM_SCHEMAS
    "${CMAKE_CURRENT_SOURCE_DIR}/schemas/midi_messages.fbs"
    "${CMAKE_CURRENT_SOURCE_DIR}/schemas/pwm_types.fbs"
    "${CMAKE_CURRENT_SOURCE_DIR}/schemas/pwm_channel_config.fbs"
    "${CMAKE_CURRENT_SOURCE_DIR}/schemas/pwm_channel_data.fbs"
    "${CMAKE_CURRENT_SOURCE_DIR}/schemas/pwm_fault_log.fbs"
    "${CMAKE_CURRENT_SOURCE_DIR}/schemas/pwm_fault_control.fbs"
    "${CMAKE_CURRENT_SOURCE_DIR}/schemas/pwm_messages.fbs"
)

set(LIBCOMM_GENERATED_HEADERS)
foreach(schema ${LIBCOMM_SCHEMAS})
    get_filename_component(schema_name "${schema}" NAME_WE)
    set(output_header "${LIBCOMM_GENERATED_DIR}/${schema_name}_generated.h")
    list(APPEND LIBCOMM_GENERATED_HEADERS "${output_header}")

    add_custom_command(
        OUTPUT "${output_header}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${LIBCOMM_GENERATED_DIR}"
        COMMAND "${LIBCOMM_FLATC_COMMAND}" --cpp --scoped-enums -I "${CMAKE_CURRENT_SOURCE_DIR}/schemas" -o "${LIBCOMM_GENERATED_DIR}" "${schema}"
        DEPENDS ${LIBCOMM_SCHEMAS}
        VERBATIM
        COMMENT "Generating FlatBuffers sources from ${schema}"
    )
endforeach()

add_custom_target(libcomm_generate ALL
    DEPENDS ${LIBCOMM_GENERATED_HEADERS}
)

add_library(libcomm STATIC
    src/endpoint.cpp
    src/frame_transport.cpp
    src/pwm_endpoint.cpp
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
