cmake_minimum_required(VERSION 3.16)

project(Shaders)

set(HEADER "${PROJECT_BINARY_DIR}/sprv/shader.h")
set(CPP    "${PROJECT_BINARY_DIR}/sprv/shader.cpp")

file(GLOB SOURCES      "${CMAKE_CURRENT_SOURCE_DIR}/*.vert" "${CMAKE_CURRENT_SOURCE_DIR}/*.frag"
                       "${CMAKE_CURRENT_SOURCE_DIR}/*.tesc" "${CMAKE_CURRENT_SOURCE_DIR}/*.tese" "${CMAKE_CURRENT_SOURCE_DIR}/*.glsl")
file(GLOB GLSL_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.glsl")

# GLSL to SPIR-V compiler (part of the Vulkan SDK)
find_program(GLSLANGVALIDATOR glslangValidator HINTS "$ENV{VULKAN_SDK}/bin" "$ENV{VULKAN_SDK}/Bin")
if(NOT GLSLANGVALIDATOR)
  message(FATAL_ERROR "glslangValidator required (install Vulkan SDK)")
endif()

function(add_shader OUTPUT SOURCE)
  set(OUTPUT_FILE "${PROJECT_BINARY_DIR}/sprv/${OUTPUT}.sprv")
  set(SOURCE_FILE "${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE}")
  set(APP_SHADERS_SPRV ${APP_SHADERS_SPRV} ${OUTPUT_FILE} PARENT_SCOPE)
  add_custom_command(
    OUTPUT     ${OUTPUT_FILE}
    DEPENDS    ${SOURCE_FILE} ${GLSL_SOURCES}
    COMMAND    ${CMAKE_COMMAND} -E make_directory "${PROJECT_BINARY_DIR}/sprv/"
    COMMAND    ${GLSLANGVALIDATOR} -V --target-env spirv1.4 "${SOURCE_FILE}" -o ${OUTPUT_FILE}
    )
endfunction(add_shader)

add_shader(level.vert level.vert)
add_shader(level.frag level.frag)
add_shader(water.tesc water.tesc)
add_shader(water.tese water.tese)
add_shader(collision.vert collision.vert)
add_shader(collision.frag collision.frag)

add_custom_command(
  OUTPUT     ${HEADER} ${CPP}
  DEPENDS    ${APP_SHADERS_SPRV}
  COMMAND    ${CMAKE_COMMAND} -E make_directory "${PROJECT_BINARY_DIR}/sprv/"
  COMMAND    ${CMAKE_COMMAND} -P "${CMAKE_CURRENT_SOURCE_DIR}/link_shaders.cmake"
  WORKING_DIRECTORY "${PROJECT_BINARY_DIR}"
  )

add_custom_target(shader
  DEPENDS ${HEADER} ${CPP}
  SOURCES ${SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/link_shaders.cmake")

add_library(${PROJECT_NAME} STATIC ${HEADER} ${CPP})
add_dependencies(${PROJECT_NAME} shader)
target_include_directories(${PROJECT_NAME} PUBLIC "${PROJECT_BINARY_DIR}/sprv")
